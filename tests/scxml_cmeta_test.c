#include <scxml/scxml.h>

#include "tinytest.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { SCXML_TEST_TEXT_CAPACITY = 95u };

typedef struct scxml_owned_text {
    size_t size;
    char data[SCXML_TEST_TEXT_CAPACITY + 1u];
} scxml_owned_text;

typedef struct scxml_borrowed_text {
    const unsigned char *data;
    size_t size;
} scxml_borrowed_text;

Struct(scxml_nested_data,
    (scxml_owned_text, invoke_id)
);

Enum(scxml_public_source,
    (SCXML_PUBLIC_SOURCE_GOOD, 1, "good"),
    (SCXML_PUBLIC_SOURCE_FAIL, 2, "fail")
);

Struct(scxml_public_data,
    (bool, enabled),
    (int, count),
    (scxml_public_source, source),
    (size_t, total),
    (double, ratio),
    (scxml_owned_text, send_id),
    (scxml_borrowed_text, borrowed_id),
    (scxml_owned_text, custom_id),
    (scxml_owned_text, readonly_id),
    (scxml_owned_text, failing_id),
    (scxml_nested_data, nested)
);

static const cmeta_type_identity public_data_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.public.data");

static atomic_size_t public_data_copy_count;
static atomic_size_t public_data_move_count;
static atomic_size_t public_data_destroy_count;

static bool public_data_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    memcpy(destination, source, sizeof(scxml_public_data));
    (void)atomic_fetch_add_explicit(
        &public_data_copy_count, 1u, memory_order_relaxed);
    return true;
}

static void public_data_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(scxml_public_data));
    memset(source, 0, sizeof(scxml_public_data));
    (void)atomic_fetch_add_explicit(
        &public_data_move_count, 1u, memory_order_relaxed);
}

static void public_data_destroy(void *value) {
    if (value == NULL) return;
    (void)atomic_fetch_add_explicit(
        &public_data_destroy_count, 1u, memory_order_relaxed);
}

static const cmeta_type_traits public_data_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = public_data_copy,
    .move_construct = public_data_move,
    .destroy = public_data_destroy
};

static const cmeta_type_desc public_data_type = {
    .name = "scxml_public_data",
    .size = sizeof(scxml_public_data),
    .align = _Alignof(scxml_public_data),
    .kind = CMETA_T_OBJECT,
    .traits = &public_data_traits,
    .identity = &public_data_identity
};

static const cmeta_type_identity public_source_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.public.source");

static const cmeta_type_desc public_source_type = {
    .name = "scxml_public_source",
    .size = sizeof(scxml_public_source),
    .align = _Alignof(scxml_public_source),
    .kind = CMETA_T_INTEGER,
    .identity = &public_source_identity
};

static bool public_source_is_zero(const void *object) {
    scxml_public_source value;
    if (object == NULL) return false;
    memcpy(&value, object, sizeof(value));
    return value == (scxml_public_source)0;
}

static cmeta_status public_source_read(const void *object, int64_t *out) {
    scxml_public_source value;
    if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(&value, object, sizeof(value));
    if (value == SCXML_PUBLIC_SOURCE_FAIL) return CMETA_CALLBACK_ERROR;
    *out = (int64_t)value;
    return CMETA_OK;
}

static cmeta_status public_source_assign(void *object, int64_t value) {
    const scxml_public_source native = (scxml_public_source)value;
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(object, &native, sizeof(native));
    return CMETA_OK;
}

static void public_source_restore_zero(void *object) {
    const scxml_public_source value = (scxml_public_source)0;
    if (object != NULL) memcpy(object, &value, sizeof(value));
}

static const cmeta_data_enum_shape public_source_shape = {
    .meta = EnumMeta(scxml_public_source)
};

static const cmeta_data_enum_ops public_source_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &public_source_type,
    .is_zero = public_source_is_zero,
    .read = public_source_read,
    .assign = public_source_assign,
    .restore_zero = public_source_restore_zero
};

static const cmeta_data_desc public_source_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.public.source.schema",
    .display_name = "SCXML public source",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &public_source_type,
    .shape = &public_source_shape,
    .enum_ops = &public_source_ops
};

static const cmeta_type_desc owned_text_type = {
    .name = "scxml_owned_text",
    .size = sizeof(scxml_owned_text),
    .align = _Alignof(scxml_owned_text),
    .kind = CMETA_T_OBJECT
};

static bool owned_text_is_zero(const void *object) {
    const scxml_owned_text *text = (const scxml_owned_text *)object;
    return text != NULL && text->size == 0u;
}

static cmeta_status owned_text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    scxml_owned_text *text = (scxml_owned_text *)object;
    if (text == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > SCXML_TEST_TEXT_CAPACITY)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) memcpy(text->data, data, size);
    text->data[size] = '\0';
    text->size = size;
    return CMETA_OK;
}

static void owned_text_restore_zero(void *object) {
    if (object != NULL) memset(object, 0, sizeof(scxml_owned_text));
}

static cmeta_status owned_text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const scxml_owned_text *text = (const scxml_owned_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        text->size > SCXML_TEST_TEXT_CAPACITY)
        return CMETA_INVALID_ARGUMENT;
    *out_data = (const unsigned char *)text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_data_buffer_shape owned_text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};

static const cmeta_data_buffer_ops owned_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &owned_text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = owned_text_is_zero,
    .assign = owned_text_assign,
    .restore_zero = owned_text_restore_zero,
    .read = owned_text_read
};

static const cmeta_data_desc owned_text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.owned.text.schema",
    .display_name = "SCXML owned text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &owned_text_type,
    .shape = &owned_text_shape,
    .buffer_ops = &owned_text_ops
};

static const cmeta_type_desc borrowed_text_type = {
    .name = "scxml_borrowed_text",
    .size = sizeof(scxml_borrowed_text),
    .align = _Alignof(scxml_borrowed_text),
    .kind = CMETA_T_OBJECT
};

static bool borrowed_text_is_zero(const void *object) {
    const scxml_borrowed_text *text = (const scxml_borrowed_text *)object;
    return text != NULL && text->data == NULL && text->size == 0u;
}

static cmeta_status borrowed_text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    scxml_borrowed_text *text = (scxml_borrowed_text *)object;
    if (text == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
    text->data = data;
    text->size = size;
    return CMETA_OK;
}

static void borrowed_text_restore_zero(void *object) {
    if (object != NULL) memset(object, 0, sizeof(scxml_borrowed_text));
}

static cmeta_status borrowed_text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const scxml_borrowed_text *text = (const scxml_borrowed_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out_data = text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_data_buffer_shape borrowed_text_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED
};

static const cmeta_data_buffer_ops borrowed_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &borrowed_text_type,
    .ownership = CMETA_DATA_BUFFER_BORROWED,
    .is_zero = borrowed_text_is_zero,
    .assign = borrowed_text_assign,
    .restore_zero = borrowed_text_restore_zero,
    .read = borrowed_text_read
};

static const cmeta_data_desc borrowed_text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.borrowed.text.schema",
    .display_name = "SCXML borrowed text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &borrowed_text_type,
    .shape = &borrowed_text_shape,
    .buffer_ops = &borrowed_text_ops
};

static const cmeta_data_buffer_ops custom_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &owned_text_type,
    .ownership = CMETA_DATA_BUFFER_CUSTOM,
    .is_zero = owned_text_is_zero,
    .assign = owned_text_assign,
    .restore_zero = owned_text_restore_zero,
    .read = owned_text_read
};

static const cmeta_data_desc custom_text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.custom.text.schema",
    .display_name = "SCXML custom text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &owned_text_type,
    .shape = &owned_text_shape,
    .buffer_ops = &custom_text_ops
};

static const cmeta_data_buffer_ops readonly_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &owned_text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = owned_text_is_zero,
    .restore_zero = owned_text_restore_zero,
    .read = owned_text_read
};

static const cmeta_data_desc readonly_text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.readonly.text.schema",
    .display_name = "SCXML read-only text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &owned_text_type,
    .shape = &owned_text_shape,
    .buffer_ops = &readonly_text_ops
};

static cmeta_status failing_text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    if (size == sizeof("old") - 1u && data != NULL &&
        memcmp(data, "old", sizeof("old") - 1u) == 0)
        return owned_text_assign(object, data, size, max_bytes);
    return CMETA_CAPACITY_EXCEEDED;
}

static const cmeta_data_buffer_ops failing_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &owned_text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = owned_text_is_zero,
    .assign = failing_text_assign,
    .restore_zero = owned_text_restore_zero,
    .read = owned_text_read
};

static const cmeta_data_desc failing_text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.failing.text.schema",
    .display_name = "SCXML failing text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &owned_text_type,
    .shape = &owned_text_shape,
    .buffer_ops = &failing_text_ops
};

static const cmeta_type_desc nested_data_type = {
    .name = "scxml_nested_data",
    .size = sizeof(scxml_nested_data),
    .align = _Alignof(scxml_nested_data),
    .kind = CMETA_T_OBJECT
};

static const cmeta_data_field_desc nested_data_fields[] = {
    {"test.scxml.nested.data.invoke_id", "invoke_id",
     offsetof(scxml_nested_data, invoke_id), &owned_text_desc}
};

static const cmeta_data_struct_shape nested_data_shape = {
    .layout = StructMeta(scxml_nested_data),
    .fields = nested_data_fields,
    .field_count = sizeof(nested_data_fields) / sizeof(nested_data_fields[0])
};

static const cmeta_data_desc nested_data_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.nested.data.schema",
    .display_name = "SCXML nested data",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &nested_data_type,
    .shape = &nested_data_shape
};

static const cmeta_data_field_desc public_data_fields[] = {
    {"test.scxml.public.data.enabled", "enabled",
     offsetof(scxml_public_data, enabled), &cmeta_data_bool},
    {"test.scxml.public.data.count", "count",
     offsetof(scxml_public_data, count), &cmeta_data_int},
    {"test.scxml.public.data.source", "source",
     offsetof(scxml_public_data, source), &public_source_desc},
    {"test.scxml.public.data.total", "total",
     offsetof(scxml_public_data, total), &cmeta_data_size},
    {"test.scxml.public.data.ratio", "ratio",
     offsetof(scxml_public_data, ratio), &cmeta_data_double},
    {"test.scxml.public.data.send_id", "send_id",
     offsetof(scxml_public_data, send_id), &owned_text_desc},
    {"test.scxml.public.data.borrowed_id", "borrowed_id",
     offsetof(scxml_public_data, borrowed_id), &borrowed_text_desc},
    {"test.scxml.public.data.custom_id", "custom_id",
     offsetof(scxml_public_data, custom_id), &custom_text_desc},
    {"test.scxml.public.data.readonly_id", "readonly_id",
     offsetof(scxml_public_data, readonly_id), &readonly_text_desc},
    {"test.scxml.public.data.failing_id", "failing_id",
     offsetof(scxml_public_data, failing_id), &failing_text_desc},
    {"test.scxml.public.data.nested", "nested",
     offsetof(scxml_public_data, nested), &nested_data_desc}
};

static const cmeta_data_struct_shape public_data_shape = {
    .layout = StructMeta(scxml_public_data),
    .fields = public_data_fields,
    .field_count = sizeof(public_data_fields) /
                   sizeof(public_data_fields[0])
};

static const cmeta_data_desc public_data_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.public.data.schema",
    .display_name = "SCXML public data",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &public_data_type,
    .shape = &public_data_shape
};

static scxml_status compile_cmeta(
    const char *source, scxml_program *program,
    scxml_diagnostic *diagnostic) {
    const scxml_cmeta_compile_options_v1 options =
        scxml_cmeta_default_compile_options(&public_data_desc);
    return scxml_compile_cmeta(
        program, source, strlen(source), NULL, &options, diagnostic);
}

typedef struct dynamic_adapter_probe {
    size_t sends;
    size_t cancels;
    size_t starts;
    size_t invoke_cancels;
    char event[32];
    char target[32];
    char type[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char source[32];
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char cancel_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char generated_send_ids[4][SCXML_EVENT_METADATA_CAPACITY + 1u];
    uint64_t delay_ms;
    scxml_session *session;
    scxml_adapter_status send_status;
    scxml_adapter_status cancel_status;
    bool report_done_during_cancel;
    bool report_done_result;
} dynamic_adapter_probe;

typedef struct payload_adapter_probe {
    size_t sends;
    size_t starts;
    size_t invoke_cancels;
    uint64_t token;
    scxml_adapter_status send_status;
    scxml_adapter_status start_status;
    bool invalid_send_ticket;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char type[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char source[SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_payload_kind kind;
    size_t entry_count;
    char names[8][32];
    scxml_payload_value values[8];
    char strings[8][32];
    scxml_payload_value content;
    char content_string[32];
} payload_adapter_probe;

typedef struct content_probe {
    size_t sends;
    size_t starts;
    size_t commits;
    size_t discards;
    scxml_adapter_status send_status;
    bool invalid_send_ticket;
    scxml_content_kind kind;
    char bytes[256];
    const cmeta_data_desc *schema;
    scxml_nested_data nested;
} content_probe;

typedef struct invoke_idlocation_probe invoke_idlocation_probe;

typedef struct invoke_idlocation_ticket {
    invoke_idlocation_probe *probe;
    size_t index;
    bool cancel;
} invoke_idlocation_ticket;

struct invoke_idlocation_probe {
    scxml_session *session;
    size_t prepare_starts;
    size_t start_commits;
    size_t start_discards;
    size_t prepare_cancels;
    size_t cancel_commits;
    size_t cancel_discards;
    size_t close_calls;
    scxml_adapter_status start_status[4];
    bool invalid_start_ticket[4];
    bool cancel_during_prepare;
    uint64_t start_tokens[4];
    char start_ids[4][SCXML_EVENT_METADATA_CAPACITY + 1u];
    char start_types[4][SCXML_EVENT_METADATA_CAPACITY + 1u];
    uint64_t cancel_tokens[4];
    char cancel_ids[4][SCXML_EVENT_METADATA_CAPACITY + 1u];
    invoke_idlocation_ticket start_tickets[4];
    invoke_idlocation_ticket cancel_tickets[4];
};

static void dynamic_ticket_done(void *user) {
    (void)user;
}

static void invoke_idlocation_ticket_commit(void *user) {
    invoke_idlocation_ticket *ticket = (invoke_idlocation_ticket *)user;
    if (ticket == NULL || ticket->probe == NULL) return;
    if (ticket->cancel)
        ++ticket->probe->cancel_commits;
    else
        ++ticket->probe->start_commits;
}

static void invoke_idlocation_ticket_discard(void *user) {
    invoke_idlocation_ticket *ticket = (invoke_idlocation_ticket *)user;
    if (ticket == NULL || ticket->probe == NULL) return;
    if (ticket->cancel)
        ++ticket->probe->cancel_discards;
    else
        ++ticket->probe->start_discards;
}

static bool copy_probe_text(char *destination, size_t capacity,
                            const char *source, size_t size) {
    if (destination == NULL || capacity == 0u || size >= capacity ||
        (size != 0u && source == NULL))
        return false;
    if (size != 0u) memcpy(destination, source, size);
    destination[size] = '\0';
    return true;
}

static scxml_adapter_status invoke_idlocation_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    invoke_idlocation_probe *probe = (invoke_idlocation_probe *)user;
    size_t index;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->prepare_starts >= 4u)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    index = probe->prepare_starts++;
    if (!copy_probe_text(
            probe->start_ids[index], sizeof(probe->start_ids[index]),
            request->id, request->id_size) ||
        !copy_probe_text(
            probe->start_types[index], sizeof(probe->start_types[index]),
            request->type, request->type_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    probe->start_tokens[index] = request->token;
    if (probe->cancel_during_prepare && probe->session != NULL)
        scxml_session_cancel(probe->session);
    if (probe->start_status[index] != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "injected invoke start rejection";
        return probe->start_status[index];
    }
    if (probe->invalid_start_ticket[index]) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        *out_error = NULL;
        return SCXML_ADAPTER_ACCEPTED;
    }
    probe->start_tickets[index] = (invoke_idlocation_ticket){
        probe, index, false};
    *out_ticket = (cflow_statechart_effect_ticket){
        invoke_idlocation_ticket_commit,
        invoke_idlocation_ticket_discard,
        &probe->start_tickets[index]};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status invoke_idlocation_prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    invoke_idlocation_probe *probe = (invoke_idlocation_probe *)user;
    size_t index;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->prepare_cancels >= 4u)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    index = probe->prepare_cancels++;
    if (!copy_probe_text(
            probe->cancel_ids[index], sizeof(probe->cancel_ids[index]),
            request->id, request->id_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    probe->cancel_tokens[index] = request->token;
    probe->cancel_tickets[index] = (invoke_idlocation_ticket){
        probe, index, true};
    *out_ticket = (cflow_statechart_effect_ticket){
        invoke_idlocation_ticket_commit,
        invoke_idlocation_ticket_discard,
        &probe->cancel_tickets[index]};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void invoke_idlocation_close(void *user) {
    invoke_idlocation_probe *probe = (invoke_idlocation_probe *)user;
    if (probe != NULL) ++probe->close_calls;
}

static bool invoke_idlocation_quiescent(void *user) {
    return user != NULL;
}

static bool copy_payload_value(
    scxml_payload_value *destination, char *string_storage,
    size_t string_capacity, const scxml_payload_value *source) {
    if (destination == NULL || string_storage == NULL || source == NULL)
        return false;
    *destination = *source;
    if (source->kind != SCXML_PAYLOAD_VALUE_STRING) return true;
    if (!copy_probe_text(
            string_storage, string_capacity, source->data.string.data,
            source->data.string.size))
        return false;
    destination->data.string.data = string_storage;
    return true;
}

static bool copy_payload(payload_adapter_probe *probe,
                         const scxml_payload_view *payload) {
    size_t index;
    if (probe == NULL || payload == NULL || payload->entry_count > 8u)
        return false;
    probe->kind = payload->kind;
    probe->entry_count = payload->entry_count;
    if (payload->kind == SCXML_PAYLOAD_CONTENT)
        return payload->content.kind == SCXML_CONTENT_SCALAR &&
        copy_payload_value(
            &probe->content, probe->content_string,
            sizeof(probe->content_string), &payload->content.scalar);
    for (index = 0u; index < payload->entry_count; ++index) {
        if (!copy_probe_text(
                probe->names[index], sizeof(probe->names[index]),
                payload->entries[index].name,
                payload->entries[index].name_size) ||
            payload->entries[index].value.kind != SCXML_CONTENT_SCALAR ||
            !copy_payload_value(
                &probe->values[index], probe->strings[index],
                sizeof(probe->strings[index]),
                &payload->entries[index].value.scalar))
            return false;
    }
    return payload->kind == SCXML_PAYLOAD_NONE ||
           payload->kind == SCXML_PAYLOAD_NAMED;
}

static bool copy_content(content_probe *probe,
                            const scxml_content_view *content) {
    if (probe == NULL || content == NULL) return false;
    probe->kind = content->kind;
    if (content->kind == SCXML_CONTENT_TEXT_UTF8 ||
        content->kind == SCXML_CONTENT_XML_UTF8) {
        return copy_probe_text(probe->bytes, sizeof(probe->bytes),
                               content->bytes, content->byte_count);
    }
    if (content->kind == SCXML_CONTENT_CMETA) {
        if (content->schema != &nested_data_desc || content->object == NULL)
            return false;
        probe->schema = content->schema;
        probe->nested = *(const scxml_nested_data *)content->object;
        return true;
    }
    return content->kind == SCXML_CONTENT_SCALAR;
}

static void content_ticket_commit(void *user) {
    content_probe *probe = (content_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void content_ticket_discard(void *user) {
    content_probe *probe = (content_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status content_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    content_probe *probe = (content_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->payload.kind !=
            SCXML_PAYLOAD_CONTENT ||
        !copy_content(probe, &request->payload.content))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->sends;
    if (probe->send_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "content-aware send rejected by test adapter";
        return probe->send_status;
    }
    if (probe->invalid_send_ticket) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        *out_error = NULL;
        return SCXML_ADAPTER_ACCEPTED;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        content_ticket_commit, content_ticket_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status content_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    content_probe *probe = (content_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->payload.kind !=
            SCXML_PAYLOAD_CONTENT ||
        !copy_content(probe, &request->payload.content))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->starts;
    *out_ticket = (cflow_statechart_effect_ticket){
        content_ticket_commit, content_ticket_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status content_prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    if (user == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, user};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status payload_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    payload_adapter_probe *probe = (payload_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || !copy_payload(probe, &request->payload))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->sends;
    if (probe->send_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "payload send rejected by test adapter";
        return probe->send_status;
    }
    if (probe->invalid_send_ticket) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        *out_error = NULL;
        return SCXML_ADAPTER_ACCEPTED;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status payload_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    payload_adapter_probe *probe = (payload_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == 0u ||
        !copy_probe_text(probe->id, sizeof(probe->id),
                         request->id, request->id_size) ||
        !copy_probe_text(probe->type, sizeof(probe->type),
                         request->type, request->type_size) ||
        !copy_probe_text(probe->source, sizeof(probe->source),
                         request->src, request->src_size) ||
        !copy_payload(probe, &request->payload))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    probe->token = request->token;
    ++probe->starts;
    if (probe->start_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "payload invoke rejected by test adapter";
        return probe->start_status;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status payload_prepare_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    payload_adapter_probe *probe = (payload_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->invoke_cancels;
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status dynamic_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    dynamic_adapter_probe *probe = (dynamic_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        !copy_probe_text(probe->event, sizeof(probe->event),
                         request->event, request->event_size) ||
        !copy_probe_text(probe->target, sizeof(probe->target),
                         request->target, request->target_size) ||
        !copy_probe_text(probe->type, sizeof(probe->type),
                         request->type, request->type_size) ||
        !copy_probe_text(probe->send_id, sizeof(probe->send_id),
                         request->id, request->id_size) ||
        (probe->sends < 4u &&
         !copy_probe_text(
             probe->generated_send_ids[probe->sends],
             sizeof(probe->generated_send_ids[probe->sends]),
             request->id, request->id_size)))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->sends;
    probe->delay_ms = request->delay_ms;
    if (probe->send_status != SCXML_ADAPTER_ACCEPTED) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        *out_error = "configured send rejection";
        return probe->send_status;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status dynamic_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    dynamic_adapter_probe *probe = (dynamic_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        !copy_probe_text(probe->send_id, sizeof(probe->send_id),
                         request->send_id, request->send_id_size) ||
        !copy_probe_text(probe->cancel_id, sizeof(probe->cancel_id),
                         request->send_id, request->send_id_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancels;
    if (probe->report_done_during_cancel) {
        probe->report_done_result = scxml_session_report_send_done(
            probe->session, request->send_id, request->send_id_size);
    }
    if (probe->cancel_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "injected dynamic cancel failure";
        return probe->cancel_status;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status dynamic_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    dynamic_adapter_probe *probe = (dynamic_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        !copy_probe_text(probe->type, sizeof(probe->type),
                         request->type, request->type_size) ||
        !copy_probe_text(probe->source, sizeof(probe->source),
                         request->src, request->src_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->starts;
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status dynamic_prepare_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    dynamic_adapter_probe *probe = (dynamic_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->invoke_cancels;
    *out_ticket = (cflow_statechart_effect_ticket){
        dynamic_ticket_done, dynamic_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void dynamic_adapter_close(void *user) {
    (void)user;
}

static bool dynamic_adapter_quiescent(void *user) {
    return user != NULL;
}

static bool run_guarded_transition(
    const scxml_program *program, scxml_public_data initial,
    bool mutate_after_init) {
    scxml_session session = {0};
    cflow_executor executor = {0};
    cflow_event_view go = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_session_config config = {
        .program = program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = 16u
    };
    scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(scxml_cmeta_session_options_v1),
        .initial_state = &initial
    };
    bool done = false;

    check_true(cflow_executor_serial_init(&executor));
    check_equal(scxml_session_init_cmeta(&session, &config, &data),
                CFLOW_STATECHART_INSTANCE_OK);
    initial.enabled = mutate_after_init ? !initial.enabled : initial.enabled;
    check_true(scxml_program_event(program, "go", 2u, &go));
    check_equal(scxml_session_try_send(&session, &go),
                CFLOW_MAILBOX_OK);
    check_true(cflow_executor_wait_idle(&executor));
    check_true(scxml_session_get_stats(&session, &stats));
    done = stats.done;
    check_equal(scxml_session_destroy(&session),
                CFLOW_STATECHART_INSTANCE_OK);
    cflow_executor_destroy(&executor);
    return done;
}

static cflow_statechart_instance_stats run_to_idle(
    const scxml_program *program, scxml_public_data initial) {
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT,
        .prepare_send = dynamic_prepare_send,
        .close = dynamic_adapter_close,
        .is_quiescent = dynamic_adapter_quiescent};
    dynamic_adapter_probe probe = {0};
    scxml_session session = {0};
    cflow_executor executor = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_session_config config = {
        .program = program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 4u,
        .completion_capacity = 2u,
        .microstep_limit = 32u,
        .effect_capacity = 2u
    };
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(scxml_cmeta_session_options_v1),
        .initial_state = &initial
    };
    uint32_t requirements = 0u;

    check_true(cflow_executor_serial_init(&executor));
    check_true(scxml_program_requirements(program, &requirements));
    if ((requirements & SCXML_REQUIREMENT_EVENT_IO) != 0u) {
        config.adapter_internal_event_capacity = 2u;
        config.event_io = &event_io;
        config.adapter_user = &probe;
    }
    check_equal(scxml_session_init_cmeta(&session, &config, &data),
                CFLOW_STATECHART_INSTANCE_OK);
    check_true(cflow_executor_wait_idle(&executor));
    check_true(scxml_session_get_stats(&session, &stats));
    check_equal(scxml_session_destroy(&session),
                CFLOW_STATECHART_INSTANCE_OK);
    cflow_executor_destroy(&executor);
    return stats;
}

static cflow_statechart_instance_stats run_direct_to_idle(
    const scxml_program *program, scxml_public_data initial,
    cflow_statechart_instance_status *out_init_status,
    cflow_statechart_instance_status *out_destroy_status) {
    const cflow_statechart_guard_binding *guards = NULL;
    const cflow_statechart_executable_binding *executables = NULL;
    size_t guard_count = 0u;
    size_t executable_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_statechart_instance_config config;

    check_true(scxml_program_guard_bindings(
        program, &guards, &guard_count));
    check_true(scxml_program_instance_bindings(
        program, &executables, &executable_count));
    check_true(cflow_executor_serial_init(&executor));
    config = (cflow_statechart_instance_config){
        .statechart = scxml_program_statechart(program),
        .initial_state = &initial,
        .guards = guards,
        .guard_count = guard_count,
        .executables = executables,
        .executable_count = executable_count,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = 16u,
        .executor = &executor
    };
    *out_init_status = cflow_statechart_instance_init(&instance, &config);
    if (*out_init_status != CFLOW_STATECHART_INSTANCE_OK) {
        *out_destroy_status = CFLOW_STATECHART_INSTANCE_OK;
        cflow_executor_destroy(&executor);
        return stats;
    }
    check_true(cflow_executor_wait_idle(&executor));
    check_true(cflow_statechart_instance_get_stats(&instance, &stats));
    *out_destroy_status = cflow_statechart_instance_destroy(&instance);
    cflow_executor_destroy(&executor);
    return stats;
}

spec("TurboSCXML public CMeta data model") {
    it("admits bounded CMeta transition conditions and copies session state") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<onentry><log label='armed'/></onentry>"
            "<transition event='go' cond='enabled &amp;&amp; count &gt;= 2 "
            "&amp;&amp; In(\"armed\")' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session legacy_session = {0};
        cflow_executor legacy_executor = {0};
        scxml_session_config legacy_config = {
            .program = &program,
            .executor = &legacy_executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u
        };
        scxml_public_data initial = {true, 2};
        scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(scxml_cmeta_session_options_v1),
            .initial_state = &initial
        };

        atomic_store_explicit(
            &public_data_copy_count, 0u, memory_order_relaxed);
        atomic_store_explicit(
            &public_data_destroy_count, 0u, memory_order_relaxed);

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&legacy_executor));
        check_equal(scxml_session_init(&legacy_session, &legacy_config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        check_null(legacy_session.impl);
        data.abi_version = 0u;
        check_equal(scxml_session_init_cmeta(
                        &legacy_session, &legacy_config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        data.abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1;
        data.initial_state = NULL;
        check_equal(scxml_session_init_cmeta(
                        &legacy_session, &legacy_config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        cflow_executor_destroy(&legacy_executor);

        check_true(run_guarded_transition(
            &program, (scxml_public_data){true, 2}, true));
        check_false(run_guarded_transition(
            &program, (scxml_public_data){false, 2}, true));
        check_false(run_guarded_transition(
            &program, (scxml_public_data){true, 1}, false));
        check_true(atomic_load_explicit(
                       &public_data_copy_count, memory_order_relaxed) > 0u);
        check_true(atomic_load_explicit(
                       &public_data_destroy_count, memory_order_relaxed) > 0u);
        scxml_program_destroy(&program);
    }

    it("validates CMeta provider contracts and preserves null admission") {
        static const char cmeta_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='only'/></scxml>";
        static const char null_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='only'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_cmeta_compile_options_v1 options =
            scxml_cmeta_default_compile_options(&public_data_desc);

        check_equal(scxml_compile(
                        &program, cmeta_source, strlen(cmeta_source), NULL,
                        &diagnostic),
                    SCXML_UNSUPPORTED_DATAMODEL);
        check_null(program.impl);
        check_equal(scxml_compile_cmeta(
                        &program, null_source, strlen(null_source), NULL,
                        &options, &diagnostic),
                    SCXML_UNSUPPORTED_DATAMODEL);
        check_null(program.impl);

        options.abi_version = 0u;
        check_equal(scxml_compile_cmeta(
                        &program, cmeta_source, strlen(cmeta_source), NULL,
                        &options, &diagnostic),
                    SCXML_INVALID_ARGUMENT);
        options = scxml_cmeta_default_compile_options(&public_data_desc);
        options.struct_size -= 1u;
        check_equal(scxml_compile_cmeta(
                        &program, cmeta_source, strlen(cmeta_source), NULL,
                        &options, &diagnostic),
                    SCXML_INVALID_ARGUMENT);
        check_null(program.impl);
    }

    it("admits top-level invoke idlocation owned strings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'>"
            "<invoke idlocation='send_id'/></state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
    }

    it("admits nested invoke idlocation owned strings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'>"
            "<invoke idlocation='nested.invoke_id'/></state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
    }

    it("rejects every unsupported invoke idlocation location class") {
        static const char empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke idlocation=''/>"
            "</state></scxml>";
        static const char malformed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='nested..invoke_id'/></state></scxml>";
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='missing'/></state></scxml>";
        static const char non_string[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='count'/></state></scxml>";
        static const char borrowed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='borrowed_id'/></state></scxml>";
        static const char custom[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='custom_id'/></state></scxml>";
        static const char readonly[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='readonly_id'/></state></scxml>";
        static const char system[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='_sessionid'/></state></scxml>";
        const char *invalid[] = {
            empty, malformed, missing, non_string, borrowed, custom,
            readonly, system};
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_cmeta(invalid[index], &program, &diagnostic),
                        SCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }
    }

    it("rejects invoke id and idlocation conflicts before profile checks") {
        static const char cmeta_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke id='literal' "
            "idlocation='send_id'/></state></scxml>";
        static const char null_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='literal' "
            "idlocation='slot'/></state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_cmeta(cmeta_source, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(scxml_compile(
                        &program, null_source, strlen(null_source), NULL,
                        &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("checks invoke idlocation source and depth boundaries") {
        static const char top_level[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/></state></scxml>";
        static const char nested[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='nested.invoke_id'/></state></scxml>";
        scxml_cmeta_compile_options_v1 options =
            scxml_cmeta_default_compile_options(&public_data_desc);
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        options.max_source_bytes = sizeof("send_id") - 1u;
        check_equal(scxml_compile_cmeta(
                        &program, top_level, strlen(top_level), NULL,
                        &options, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
        options.max_source_bytes = sizeof("send_id") - 2u;
        check_equal(scxml_compile_cmeta(
                        &program, top_level, strlen(top_level), NULL,
                        &options, &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        options = scxml_cmeta_default_compile_options(
            &public_data_desc);
        options.max_path_depth = 2u;
        check_equal(scxml_compile_cmeta(
                        &program, nested, strlen(nested), NULL,
                        &options, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
        options.max_path_depth = 1u;
        check_equal(scxml_compile_cmeta(
                        &program, nested, strlen(nested), NULL,
                        &options, &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
    }

    it("checks the maximum dynamic done invoke name budget") {
        enum {
            TOKEN_DIGITS = 20u,
            DONE_PREFIX_SIZE = sizeof("done.invoke.") - 1u,
            OWNER_AT_LIMIT = SCXML_EVENT_METADATA_CAPACITY -
                TOKEN_DIGITS - DONE_PREFIX_SIZE - 1u
        };
        char owner[OWNER_AT_LIMIT + 2u];
        char source[768];
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        int written;

        owner[0] = 's';
        memset(owner + 1u, 'a', OWNER_AT_LIMIT - 1u);
        owner[OWNER_AT_LIMIT] = '\0';
        written = snprintf(
            source, sizeof(source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='%s'><invoke "
            "idlocation='send_id'/></state></scxml>", owner);
        check_true(written > 0 && (size_t)written < sizeof(source));
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);

        owner[OWNER_AT_LIMIT] = 'a';
        owner[OWNER_AT_LIMIT + 1u] = '\0';
        written = snprintf(
            source, sizeof(source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='%s'><invoke "
            "idlocation='send_id'/></state></scxml>", owner);
        check_true(written > 0 && (size_t)written < sizeof(source));
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_null(program.impl);
    }

    it("selects the first true CMeta executable partition from staged state") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='count' expr='2'/>"
            "<if cond='count == 2'><assign location='enabled' expr='false'/>"
            "<elseif cond='true'/><assign location='count' expr='3'/>"
            "<else/><assign location='count' expr='4'/></if>"
            "</onentry><transition cond='count == 2 &amp;&amp; !enabled' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("treats declared pseudo states as inactive in CMeta conditions") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='active'>"
            "<state id='active' initial='leaf'>"
            "<history id='memory'><transition target='leaf'/></history>"
            "<state id='leaf'><transition cond='In(&quot;memory&quot;)' "
            "target='fail'/><transition target='done'/></state></state>"
            "<final id='done'/><state id='fail'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("evaluates nested CMeta partitions with session system strings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' name='Checkout'><state id='active'><onentry>"
            "<if cond='_name == &quot;Checkout&quot; &amp;&amp; "
            "_sessionid != &quot;&quot;'><if cond='enabled'>"
            "<assign location='count' expr='7'/><else/>"
            "<assign location='count' expr='8'/></if><else/>"
            "<assign location='count' expr='9'/></if></onentry>"
            "<transition cond='count == 7' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("treats a failed CMeta executable condition as false and raises error.execution") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<if cond='source == 1'><assign location='count' expr='9'/>"
            "<else/><assign location='count' expr='2'/></if></onentry>"
            "<transition event='error.execution' cond='count == 2' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_FAIL});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("rejects invalid CMeta executable conditions and keeps finalize separate") {
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<if><log label='bad'/></if></onentry></state></scxml>";
        static const char empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<if cond=''><log label='bad'/></if></onentry></state></scxml>";
        static const char non_boolean[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<if cond='count'><log label='bad'/></if>"
            "</onentry></state></scxml>";
        static const char syntax[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<if cond='enabled &amp;&amp;'><log label='bad'/></if>"
            "</onentry></state></scxml>";
        static const char finalize[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><invoke id='job'>"
            "<finalize><if cond='enabled'><log label='bad'/></if>"
            "</finalize></invoke></state></scxml>";
        const char *invalid[] = {missing, empty, non_boolean, syntax};
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_cmeta(invalid[index], &program, &diagnostic),
                        SCXML_INVALID_STRUCTURE);
            check_null(program.impl);
            check_true(diagnostic.location.line > 0u);
        }
        {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_cmeta(finalize, &program, &diagnostic),
                        SCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }
    }

    it("commits ordered scalar assignments before later guards") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='count' expr='2'/></onentry>"
            "<transition cond='count == 2' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 1, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("binds immutable machine and generated session strings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' name='Checkout'><state id='active'>"
            "<onentry><assign location='enabled' "
            "expr='_name == &quot;Checkout&quot;'/></onentry>"
            "<transition cond='enabled &amp;&amp; _sessionid != &quot;&quot;' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){false, 1, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("binds current external and internal event names to CMeta expressions") {
        static const char external_guard[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='go' cond='_event.name == &quot;go&quot;' "
            "target='done'/></state><final id='done'/></scxml>";
        static const char internal_guard[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<raise event='tick'/></onentry>"
            "<transition event='tick' "
            "cond='_event.name == &quot;tick&quot; &amp;&amp; "
            "_event.type == &quot;internal&quot; &amp;&amp; "
            "_event.sendid == &quot;&quot; &amp;&amp; "
            "_event.origin == &quot;&quot; &amp;&amp; "
            "_event.origintype == &quot;&quot; &amp;&amp; "
            "_event.invokeid == &quot;&quot; &amp;&amp; "
            "_event.data == &quot;&quot;' target='done'/>"
            "</state><final id='done'/></scxml>";
        static const char executable_condition[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='go' target='matched'/></state>"
            "<state id='matched'><onentry>"
            "<if cond='_event.name == &quot;go&quot;'>"
            "<assign location='count' expr='7'/></if></onentry>"
            "<transition cond='count == 7' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(external_guard, &program, &diagnostic),
                    SCXML_OK);
        check_true(run_guarded_transition(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD}, false));
        scxml_program_destroy(&program);

        check_equal(compile_cmeta(internal_guard, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);

        check_equal(compile_cmeta(executable_condition, &program, &diagnostic),
                    SCXML_OK);
        check_true(run_guarded_transition(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD}, false));
        scxml_program_destroy(&program);
    }

    it("binds one owned external event envelope through eventless stabilization") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='go' cond='_event.name == &quot;go&quot; "
            "&amp;&amp; _event.type == &quot;external&quot; "
            "&amp;&amp; _event.sendid == &quot;send-7&quot; "
            "&amp;&amp; _event.origin == &quot;https://origin.example&quot; "
            "&amp;&amp; _event.origintype == &quot;scxml&quot; "
            "&amp;&amp; _event.invokeid == &quot;worker&quot; "
            "&amp;&amp; _event.data == &quot;payload&quot; "
            "&amp;&amp; _ioprocessors.scxml.location != &quot;&quot;' "
            "target='matched'/></state><state id='matched'>"
            "<transition cond='_event.name == &quot;go&quot; "
            "&amp;&amp; _event.data == &quot;payload&quot;' target='done'/>"
            "</state><final id='done'/></scxml>";
        const scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata),
            .send_id = "send-7", .send_id_size = sizeof("send-7") - 1u,
            .origin = "https://origin.example",
            .origin_size = sizeof("https://origin.example") - 1u,
            .origin_type = "scxml",
            .origin_type_size = sizeof("scxml") - 1u,
            .invoke_id = "worker",
            .invoke_id_size = sizeof("worker") - 1u,
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = "payload",
                .byte_count = sizeof("payload") - 1u}};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view go = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(scxml_cmeta_session_options_v1),
            .initial_state = &initial};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("owns structured event data through eventless stabilization") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='go' "
            "cond='_event.type == &quot;external&quot; &amp;&amp; "
            "_event.data.count == 42' target='matched'/></state>"
            "<state id='matched'><transition "
            "cond='_event.data.count == 42' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_public_data event_data = {
            true, 42, SCXML_PUBLIC_SOURCE_GOOD};
        scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata),
            .data = {
                .kind = SCXML_CONTENT_CMETA,
                .schema = &public_data_desc,
                .object = &event_data}};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view go = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u};
        size_t copies_before_send;

        check_true(sizeof(event_data) <= SCXML_EVENT_DATA_CAPACITY);
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "go", 2u, &go));
        copies_before_send = atomic_load_explicit(
            &public_data_copy_count, memory_order_relaxed);
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_OK);
        check_true(atomic_load_explicit(
                       &public_data_copy_count, memory_order_relaxed) >
                   copies_before_send);
        event_data.count = 7;
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("fails a scalar read of structured event data") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='go' cond='_event.data == &quot;&quot;' "
            "target='wrong'/></state><final id='wrong'/></scxml>";
        scxml_public_data event_data = {
            true, 42, SCXML_PUBLIC_SOURCE_GOOD};
        scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata),
            .data = {
                .kind = SCXML_CONTENT_CMETA,
                .schema = &public_data_desc,
                .object = &event_data}};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view go = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 16u};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_true(stats.errored);
        check_equal(stats.last_status, CFLOW_STATECHART_INSTANCE_GUARD_FAILED);
        check_equal(stats.external_failed, UINT64_C(1));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("rejects invalid structured event envelopes without consuming capacity") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='go' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_public_data event_data = {
            true, 42, SCXML_PUBLIC_SOURCE_GOOD};
        scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata),
            .data = {
                .kind = SCXML_CONTENT_CMETA,
                .schema = &public_data_desc,
                .object = &event_data}};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view go = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 16u};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "go", 2u, &go));
        metadata.abi_version = 0u;
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        metadata.abi_version = SCXML_EVENT_METADATA_ABI;
        metadata.struct_size = sizeof(metadata) + 1u;
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        metadata.struct_size = sizeof(metadata);
        metadata.data.schema = &nested_data_desc;
        metadata.data.object = &event_data.nested;
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        metadata.data.schema = &public_data_desc;
        metadata.data.object = &event_data;
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &go, &metadata),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("invalidates retained optional metadata when the next event is selected") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<transition event='first' "
            "cond='_event.sendid == &quot;s1&quot; &amp;&amp; "
            "_event.data == &quot;payload&quot;' target='retained'/></state>"
            "<state id='retained'><transition "
            "cond='_event.sendid == &quot;s1&quot; &amp;&amp; "
            "_event.data == &quot;payload&quot;' target='waiting'/></state>"
            "<state id='waiting'><transition event='second' "
            "cond='_event.name == &quot;second&quot; &amp;&amp; "
            "_event.type == &quot;external&quot; &amp;&amp; "
            "_event.sendid == &quot;&quot; &amp;&amp; "
            "_event.origin == &quot;&quot; &amp;&amp; "
            "_event.origintype == &quot;&quot; &amp;&amp; "
            "_event.invokeid == &quot;&quot; &amp;&amp; "
            "_event.data == &quot;&quot;' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata),
            .send_id = "s1", .send_id_size = sizeof("s1") - 1u,
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = "payload",
                .byte_count = sizeof("payload") - 1u}};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view first = {0};
        cflow_event_view second = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(
            &program, "first", sizeof("first") - 1u, &first));
        check_true(scxml_program_event(
            &program, "second", sizeof("second") - 1u, &second));
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &first, &metadata),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_false(stats.done);
        check_equal(scxml_session_try_send(&session, &second),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("evaluates dynamic internal send attributes and scalar content once") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send eventexpr='&quot;advance&quot;' "
            "targetexpr='&quot;#_internal&quot;'>"
            "<content expr='&quot;payload&quot;'/></send></onentry>"
            "<transition event='advance' "
            "cond='_event.type == &quot;internal&quot; &amp;&amp; "
            "_event.sendid == &quot;&quot; &amp;&amp; "
            "_event.origin == &quot;&quot; &amp;&amp; "
            "_event.origintype == &quot;&quot; &amp;&amp; "
            "_event.invokeid == &quot;&quot; &amp;&amp; "
            "_event.data == &quot;payload&quot;' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 0, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("requires payload capability for a dynamically external scalar send") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' targetexpr='&quot;peer&quot;'>"
            "<content expr='&quot;payload&quot;'/></send></onentry>"
            "<transition event='error.execution' target='done'/></state>"
            "<final id='done'/></scxml>";
        dynamic_adapter_probe probe = {0};
        scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND,
            .prepare_send = dynamic_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        uint32_t requirements = 0u;
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .event_io = &event_io,
            .adapter_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(
            &program, &requirements));
        check_true((requirements & SCXML_REQUIREMENT_PAYLOAD) != 0u);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(
                        &session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        event_io.capabilities |= SCXML_EVENT_IO_CAP_PAYLOAD;
        check_equal(scxml_session_init_cmeta(
                        &session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_true(scxml_session_get_stats(&session, &stats));
        check_false(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("materializes dynamic send cancel and invoke requests at execution") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send eventexpr='&quot;out&quot;' "
            "targetexpr='&quot;peer&quot;' typeexpr='&quot;urn:test&quot;' "
            "id='job' delayexpr='5'/><cancel sendidexpr='&quot;job&quot;'/>"
            "</onentry><invoke id='worker' "
            "typeexpr='&quot;worker.type&quot;' "
            "srcexpr='&quot;worker://one&quot;'/><transition event='out'/>"
            "<transition event='finish' target='done'/></state>"
            "<final id='done'/></scxml>";
        dynamic_adapter_probe probe = {0};
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL,
            .prepare_send = dynamic_prepare_send,
            .prepare_cancel = dynamic_prepare_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL,
            .prepare_start = dynamic_prepare_start,
            .prepare_cancel = dynamic_prepare_invoke_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view finish = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 4u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 4u,
            .adapter_internal_event_capacity = 2u,
            .delayed_send_capacity = 1u,
            .event_io = &event_io,
            .adapter_user = &probe,
            .invocation_capacity = 1u,
            .invoke = &invoke,
            .invoke_user = &probe};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(scxml_cmeta_session_options_v1),
            .initial_state = &initial};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_equal(probe.cancels, (size_t)1u);
        check_equal(probe.starts, (size_t)1u);
        check_equal(probe.event, "out", sizeof("out"));
        check_equal(probe.target, "peer", sizeof("peer"));
        check_equal(probe.type, "worker.type", sizeof("worker.type"));
        check_equal(probe.source, "worker://one", sizeof("worker://one"));
        check_equal(probe.send_id, "job", sizeof("job"));
        check_equal(probe.delay_ms, UINT64_C(5));
        check_true(scxml_program_event(&program, "finish", 6u,
                                             &finish));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_equal(probe.invoke_cancels, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("admits send idlocation only for writable owned CMeta strings") {
        static const char accepted[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' idlocation='send_id'/>"
            "</onentry></state></scxml>";
        static const char numeric[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' idlocation='count'/>"
            "</onentry></state></scxml>";
        static const char borrowed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' "
            "idlocation='borrowed_id'/></onentry></state></scxml>";
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' idlocation='missing'/>"
            "</onentry></state></scxml>";
        static const char system[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' idlocation='_sessionid'/>"
            "</onentry></state></scxml>";
        static const char conflicting[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' id='fixed' "
            "idlocation='send_id'/></onentry></state></scxml>";
        const char *invalid[] = {
            numeric, borrowed, missing, system, conflicting};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        size_t index;

        check_equal(compile_cmeta(accepted, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            check_equal(compile_cmeta(invalid[index], &program, &diagnostic),
                        SCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }
    }

    it("writes fresh send ids to staged state and passes them to the adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='out' target='peer' idlocation='send_id'/>"
            "</onentry><transition event='again' target='active'/>"
            "<transition event='finish' cond='send_id != &quot;&quot;' "
            "target='done'/></state><final id='done'/></scxml>";
        dynamic_adapter_probe probe = {0};
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND,
            .prepare_send = dynamic_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view again = {0};
        cflow_event_view finish = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .event_io = &event_io,
            .adapter_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_true(probe.generated_send_ids[0][0] != '\0');
        check_true(scxml_program_event(&program, "again", 5u, &again));
        check_equal(scxml_session_try_send(&session, &again),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)2u);
        check_not_equal(strcmp(probe.generated_send_ids[0],
                               probe.generated_send_ids[1]), 0);
        check_true(scxml_program_event(&program, "finish", 6u,
                                             &finish));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("preserves generated failed-send identity in platform error metadata") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='count' expr='99'/>"
            "<send event='out' target='peer' idlocation='send_id'/>"
            "</onentry><transition event='error.communication' "
            "cond='count == 7 &amp;&amp; send_id != &quot;&quot; &amp;&amp; "
            "_event.sendid == send_id &amp;&amp; "
            "_event.type == &quot;platform&quot;' target='done'/>"
            "<transition event='error.communication' target='failed'/>"
            "</state><state id='failed'/><final id='done'/></scxml>";
        dynamic_adapter_probe probe = {
            .send_status = SCXML_ADAPTER_ERROR_COMMUNICATION};
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND,
            .prepare_send = dynamic_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            .count = 7};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .event_io = &event_io,
            .adapter_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("uses one generated id for delayed send state and cancellation") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='out' target='peer' delay='5ms' "
            "idlocation='send_id'/><cancel sendidexpr='send_id'/>"
            "</onentry><transition cond='send_id != &quot;&quot;' "
            "target='done'/></state><final id='done'/></scxml>";
        dynamic_adapter_probe probe = {0};
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL,
            .prepare_send = dynamic_prepare_send,
            .prepare_cancel = dynamic_prepare_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 3u,
            .adapter_internal_event_capacity = 2u,
            .delayed_send_capacity = 1u,
            .event_io = &event_io,
            .adapter_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_equal(probe.cancels, (size_t)1u);
        check_equal(probe.generated_send_ids[0], probe.cancel_id,
                    sizeof(probe.cancel_id));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("records delayed completion that wins during cancel prepare") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='out' target='peer' delay='5ms' "
            "idlocation='send_id'/></onentry>"
            "<transition event='cancel'><cancel sendidexpr='send_id'/>"
            "</transition><transition event='retry'>"
            "<send event='out' target='peer' delay='5ms' "
            "idlocation='send_id'/></transition>"
            "<transition event='finish' target='done'/></state>"
            "<final id='done'/></scxml>";
        dynamic_adapter_probe probe = {
            .cancel_status = SCXML_ADAPTER_ERROR_COMMUNICATION,
            .report_done_during_cancel = true};
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL,
            .prepare_send = dynamic_prepare_send,
            .prepare_cancel = dynamic_prepare_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view cancel = {0};
        cflow_event_view retry = {0};
        cflow_event_view finish = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 3u,
            .internal_event_capacity = 3u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .delayed_send_capacity = 1u,
            .event_io = &event_io,
            .adapter_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        probe.session = &session;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_true(scxml_program_event(
            &program, "cancel", sizeof("cancel") - 1u, &cancel));
        check_equal(scxml_session_try_send(&session, &cancel),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(probe.report_done_result);
        check_false(scxml_session_report_send_done(
            &session, probe.cancel_id, strlen(probe.cancel_id)));
        check_true(scxml_program_event(
            &program, "retry", sizeof("retry") - 1u, &retry));
        check_equal(scxml_session_try_send(&session, &retry),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)2u);
        check_not_equal(strcmp(probe.generated_send_ids[0],
                               probe.generated_send_ids[1]), 0);
        check_true(scxml_program_event(
            &program, "finish", sizeof("finish") - 1u, &finish));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_true(scxml_session_report_send_done(
            &session, probe.generated_send_ids[1],
            strlen(probe.generated_send_ids[1])));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("writes internal send idlocation without an Event IO adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<send event='tick' target='#_internal' idlocation='send_id'/>"
            "</onentry><transition event='tick' "
            "cond='send_id != &quot;&quot;' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(&program, (scxml_public_data){0});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("admits ordered send and invoke scalar payload declarations") {
        static const char send_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer' "
            "namelist='count count total ratio'>"
            "<param name='enabledCopy' expr='enabled'/>"
            "<param name='sourceCopy' location='source'/>"
            "</send></onentry></state></scxml>";
        static const char invoke_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<invoke id='worker' type='urn:test' namelist='count source'/>"
            "</state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_cmeta(send_source, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
        check_equal(compile_cmeta(invoke_source, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
    }

    it("rejects payload combinations forbidden by SCXML") {
        static const char send_content_param[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send target='peer'><content expr='count'/>"
            "<param name='copy' expr='count'/></send>"
            "</onentry></state></scxml>";
        static const char invoke_namelist_param[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<invoke namelist='count'><param name='copy' expr='count'/>"
            "</invoke></state></scxml>";
        static const char param_expr_location[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer'><param name='copy' expr='count' "
            "location='count'/></send></onentry></state></scxml>";
        static const char invoke_content_unknown_attribute[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><invoke>"
            "<content expr='count' unknown='value'/></invoke>"
            "</state></scxml>";
        static const char send_namelist_literal[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer' namelist='1'/>"
            "</onentry></state></scxml>";
        static const char send_param_location_literal[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer'>"
            "<param name='copy' location='1'/></send>"
            "</onentry></state></scxml>";
        static const char invoke_param_location_literal[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><invoke>"
            "<param name='copy' location='1'/></invoke>"
            "</state></scxml>";
        const char *invalid[] = {
            send_content_param, invoke_namelist_param,
            param_expr_location, invoke_content_unknown_attribute,
            send_namelist_literal, send_param_location_literal,
            invoke_param_location_literal};
        const scxml_status expected[] = {
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE};
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_cmeta(invalid[index], &program, &diagnostic),
                        expected[index]);
            check_null(program.impl);
        }
    }

    it("transports ordered typed send payloads through the adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer' "
            "namelist='count count total ratio'>"
            "<param name='enabledCopy' expr='enabled'/>"
            "<param name='sourceCopy' location='source'/>"
            "</send></onentry></state></scxml>";
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_PAYLOAD,
            .prepare_send = payload_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        payload_adapter_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {
            true, 7, SCXML_PUBLIC_SOURCE_GOOD, 11u, 2.5};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u};
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.event_io = &event_io;
        config.adapter_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_equal(probe.kind, SCXML_PAYLOAD_NAMED);
        check_equal(probe.entry_count, (size_t)6u);
        check_equal(probe.names[0], "count", sizeof("count"));
        check_equal(probe.names[1], "count", sizeof("count"));
        check_equal(probe.names[2], "total", sizeof("total"));
        check_equal(probe.names[3], "ratio", sizeof("ratio"));
        check_equal(probe.names[4], "enabledCopy", sizeof("enabledCopy"));
        check_equal(probe.names[5], "sourceCopy", sizeof("sourceCopy"));
        check_equal(probe.values[0].kind, SCXML_PAYLOAD_VALUE_SINT);
        check_equal(probe.values[0].data.sint, INT64_C(7));
        check_equal(probe.values[1].data.sint, INT64_C(7));
        check_equal(probe.values[2].kind, SCXML_PAYLOAD_VALUE_UINT);
        check_equal(probe.values[2].data.uint, UINT64_C(11));
        check_equal(probe.values[3].kind, SCXML_PAYLOAD_VALUE_FLOAT);
        check_equal(probe.values[3].data.number, 2.5);
        check_equal(probe.values[4].kind, SCXML_PAYLOAD_VALUE_BOOL);
        check_true(probe.values[4].data.boolean);
        check_equal(probe.values[5].kind, SCXML_PAYLOAD_VALUE_SINT);
        check_equal(probe.values[5].data.sint, INT64_C(1));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("raises error.execution without reserving a send when payload evaluation fails") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer'>"
            "<param name='sourceCopy' location='source'/>"
            "</send></onentry><transition event='error.execution' "
            "target='done'/></state><final id='done'/></scxml>";
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_PAYLOAD,
            .prepare_send = payload_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        payload_adapter_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 7, SCXML_PUBLIC_SOURCE_FAIL};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u};
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.event_io = &event_io;
        config.adapter_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)0u);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("handles payload-aware send rejection and validates accepted tickets") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer' namelist='count'/>"
            "</onentry><transition event='error.communication' "
            "target='done'/></state><final id='done'/></scxml>";
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_PAYLOAD,
            .prepare_send = payload_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        const scxml_public_data initial = {
            true, 7, SCXML_PUBLIC_SOURCE_GOOD};
        size_t index;

        for (index = 0u; index < 2u; ++index) {
            payload_adapter_probe probe = {
                .send_status = index == 0u
                    ? SCXML_ADAPTER_FULL
                    : SCXML_ADAPTER_ACCEPTED,
                .invalid_send_ticket = index != 0u};
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            scxml_session session = {0};
            cflow_executor executor = {0};
            cflow_statechart_instance_stats stats = {0};
            const scxml_cmeta_session_options_v1 data = {
                .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
                .struct_size = sizeof(data),
                .initial_state = &initial};
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u};
            check_equal(compile_cmeta(source, &program, &diagnostic),
                        SCXML_OK);
            check_true(cflow_executor_serial_init(&executor));
            config.event_io = &event_io;
            config.adapter_user = &probe;
            check_equal(scxml_session_init_cmeta(
                            &session, &config, &data),
                        index == 0u
                            ? CFLOW_STATECHART_INSTANCE_OK
                            : CFLOW_STATECHART_INSTANCE_ACTION_FAILED);
            check_true(cflow_executor_wait_idle(&executor));
            check_equal(probe.sends, (size_t)1u);
            if (index == 0u) {
                check_true(scxml_session_get_stats(&session, &stats));
                check_true(stats.done);
                check_false(stats.errored);
                check_equal(scxml_session_destroy(&session),
                            CFLOW_STATECHART_INSTANCE_OK);
            } else {
                check_null(session.impl);
            }
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("requires an exact payload-capable session contract") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer' namelist='count'/>"
            "</onentry></state></scxml>";
        payload_adapter_probe payload_probe = {0};
        scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND,
            .prepare_send = payload_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {
            true, 7, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .event_io = &event_io,
            .adapter_user = &payload_probe};
        uint32_t requirements = 0u;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(
            &program, &requirements));
        check_true((requirements & SCXML_REQUIREMENT_PAYLOAD) != 0u);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(
                        &session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        event_io.capabilities |= SCXML_EVENT_IO_CAP_PAYLOAD;
        event_io.abi_version = SCXML_ADAPTER_ABI + 1u;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        event_io.abi_version = SCXML_ADAPTER_ABI;
        event_io.struct_size = sizeof(event_io) + 1u;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        event_io.struct_size = sizeof(event_io);
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(payload_probe.sends, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("transports invoke params through the adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<invoke id='worker' type='urn:test'>"
            "<param name='label' expr='&quot;worker&quot;'/><param "
            "name='countCopy' location='count'/></invoke>"
            "<transition event='finish' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_PAYLOAD,
            .prepare_start = payload_prepare_start,
            .prepare_cancel = payload_prepare_invoke_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        payload_adapter_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view finish = {0};
        const scxml_public_data initial = {
            true, 9, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 4u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u};
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.invoke = &invoke;
        config.invoke_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.starts, (size_t)1u);
        check_equal(probe.kind, SCXML_PAYLOAD_NAMED);
        check_equal(probe.entry_count, (size_t)2u);
        check_equal(probe.names[0], "label", sizeof("label"));
        check_equal(probe.values[0].kind, SCXML_PAYLOAD_VALUE_STRING);
        check_equal(probe.strings[0], "worker", sizeof("worker"));
        check_equal(probe.names[1], "countCopy", sizeof("countCopy"));
        check_equal(probe.values[1].data.sint, INT64_C(9));
        check_true(scxml_program_event(
            &program, "finish", 6u, &finish));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.invoke_cancels, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("transports scalar invoke content through the adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<invoke id='worker' type='urn:test'><content "
            "expr='&quot;markup&quot;'/></invoke>"
            "<transition event='finish' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(invoke),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_PAYLOAD,
            .prepare_start = payload_prepare_start,
            .prepare_cancel = payload_prepare_invoke_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        payload_adapter_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view finish = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 4u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u};
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.invoke = &invoke;
        config.invoke_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.starts, (size_t)1u);
        check_equal(probe.kind, SCXML_PAYLOAD_CONTENT);
        check_equal(probe.content.kind, SCXML_PAYLOAD_VALUE_STRING);
        check_equal(probe.content_string, "markup", sizeof("markup"));
        check_true(scxml_program_event(
            &program, "finish", 6u, &finish));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("materializes invoke arguments from staged onentry data before start") {
        static const char dynamic_strings[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<assign location='send_id' "
            "expr='&quot;http://www.w3.org/TR/scxml/&quot;'/><assign "
            "location='nested.invoke_id' "
            "expr='&quot;worker://runtime&quot;'/></onentry>"
            "<invoke typeexpr='send_id' "
            "srcexpr='nested.invoke_id'/><transition event='finish' "
            "target='done'/></state><final id='done'/></scxml>";
        static const char dynamic_content[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<assign location='count' expr='7'/></onentry>"
            "<invoke id='worker' type='http://www.w3.org/TR/scxml/'>"
            "<content expr='count'/></invoke><transition event='finish' "
            "target='done'/></state><final id='done'/></scxml>";
        const char *sources[] = {dynamic_strings, dynamic_content};
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(invoke),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_PAYLOAD,
            .prepare_start = payload_prepare_start,
            .prepare_cancel = payload_prepare_invoke_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        size_t index;

        for (index = 0u; index < 2u; ++index) {
            payload_adapter_probe probe = {0};
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            scxml_session session = {0};
            cflow_executor executor = {0};
            cflow_event_view finish = {0};
            const scxml_public_data initial = {0};
            const scxml_cmeta_session_options_v1 data = {
                .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
                .struct_size = sizeof(data),
                .initial_state = &initial};
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 4u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invocation_capacity = 1u};
            scxml_status compile_status;

            compile_status = compile_cmeta(
                sources[index], &program, &diagnostic);
            if (compile_status != SCXML_OK)
                info("case=%zu diagnostic=%s", index, diagnostic.message);
            check_equal(compile_status, SCXML_OK);
            check_true(cflow_executor_serial_init(&executor));
            config.invoke = &invoke;
            config.invoke_user = &probe;
            check_equal(scxml_session_init_cmeta(
                            &session, &config, &data),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_true(cflow_executor_wait_idle(&executor));
            check_equal(probe.starts, (size_t)1u);
            check_not_equal(probe.token, UINT64_C(0));
            check_equal(probe.type, "http://www.w3.org/TR/scxml/",
                        sizeof("http://www.w3.org/TR/scxml/"));
            if (index == 0u) {
                check_equal(probe.id, "armed.invoke.1",
                            sizeof("armed.invoke.1"));
                check_equal(probe.source, "worker://runtime",
                            sizeof("worker://runtime"));
                check_equal(probe.kind, SCXML_PAYLOAD_NONE);
            } else {
                check_equal(probe.id, "worker", sizeof("worker"));
                check_equal(probe.source, "", sizeof(""));
                check_equal(probe.kind, SCXML_PAYLOAD_CONTENT);
                check_equal(probe.content.kind, SCXML_PAYLOAD_VALUE_SINT);
                check_equal(probe.content.data.sint, INT64_C(7));
            }
            check_true(scxml_program_event(
                &program, "finish", sizeof("finish") - 1u, &finish));
            check_equal(scxml_session_try_send(&session, &finish),
                        CFLOW_MAILBOX_OK);
            check_true(cflow_executor_wait_idle(&executor));
            check_equal(scxml_session_destroy(&session),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("maps invoke payload evaluation and adapter failures to error.execution") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<invoke id='worker' type='urn:test'>"
            "<param name='sourceCopy' location='source'/></invoke>"
            "<transition event='error.execution' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(invoke),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_PAYLOAD,
            .prepare_start = payload_prepare_start,
            .prepare_cancel = payload_prepare_invoke_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        const scxml_public_data initial[] = {
            {true, 0, SCXML_PUBLIC_SOURCE_FAIL},
            {true, 0, SCXML_PUBLIC_SOURCE_GOOD}};
        const scxml_adapter_status start_status[] = {
            SCXML_ADAPTER_ACCEPTED,
            SCXML_ADAPTER_ERROR_EXECUTION};
        size_t index;

        for (index = 0u; index < 2u; ++index) {
            payload_adapter_probe probe = {
                .start_status = start_status[index]};
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            scxml_session session = {0};
            cflow_executor executor = {0};
            cflow_statechart_instance_stats stats = {0};
            scxml_invoke_stats invoke_stats = {0};
            const scxml_cmeta_session_options_v1 data = {
                .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
                .struct_size = sizeof(data),
                .initial_state = &initial[index]};
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 4u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invocation_capacity = 1u};
            check_equal(compile_cmeta(source, &program, &diagnostic),
                        SCXML_OK);
            check_true(cflow_executor_serial_init(&executor));
            config.invoke = &invoke;
            config.invoke_user = &probe;
            check_equal(scxml_session_init_cmeta(
                            &session, &config, &data),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_true(cflow_executor_wait_idle(&executor));
            check_equal(probe.starts, index);
            check_true(scxml_session_get_stats(&session, &stats));
            check_true(stats.done);
            check_false(stats.errored);
            check_true(scxml_session_get_invoke_stats(
                &session, &invoke_stats));
            check_equal(invoke_stats.start_failed, UINT64_C(1));
            check_equal(invoke_stats.active, (size_t)0u);
            check_equal(scxml_session_destroy(&session),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("clears provenance when an invoke error reuses an internal slot") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='sending'><onentry>"
            "<send event='out' target='peer' id='sent'/></onentry>"
            "<transition event='error.communication' target='invoking'/>"
            "</state><state id='invoking'>"
            "<invoke id='worker' type='urn:test'>"
            "<param name='sourceCopy' location='source'/></invoke>"
            "<transition event='error.execution' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_PAYLOAD,
            .prepare_send = payload_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(invoke),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_PAYLOAD,
            .prepare_start = payload_prepare_start,
            .prepare_cancel = payload_prepare_invoke_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        payload_adapter_probe probe = {
            .send_status = SCXML_ADAPTER_ERROR_COMMUNICATION};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_FAIL};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 1u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u};
        check_equal(compile_cmeta(source, &program, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.event_io = &event_io;
        config.adapter_user = &probe;
        config.invoke = &invoke;
        config.invoke_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_equal(probe.starts, (size_t)0u);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("publishes a stable invoke idlocation before committing a start") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id' typeexpr='send_id'/></state></scxml>";
        const scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL,
            .prepare_start = invoke_idlocation_prepare_start,
            .prepare_cancel = invoke_idlocation_prepare_cancel,
            .close = invoke_idlocation_close,
            .is_quiescent = invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_starts, (size_t)1u);
        check_equal(probe.start_commits, (size_t)1u);
        check_equal(probe.start_discards, (size_t)0u);
        check_equal(probe.start_tokens[0], UINT64_C(1));
        check_equal(probe.start_ids[0], "worker.1", sizeof("worker.1"));
        check_equal(probe.start_types[0], "worker.1", sizeof("worker.1"));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("does not evaluate invoke idlocation for a transient state") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='transient'><invoke "
            "idlocation='send_id'/><transition target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_starts, (size_t)0u);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("allocates a distinct invoke idlocation token on re-entry") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='worker'><state id='worker'><invoke "
            "idlocation='send_id'/><transition event='leave' target='idle'/>"
            "</state><state id='idle'><transition event='again' "
            "target='worker'/></state></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view leave = {0};
        cflow_event_view again = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "leave", 5u, &leave));
        check_true(scxml_program_event(&program, "again", 5u, &again));
        check_equal(scxml_session_try_send(&session, &leave),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_try_send(&session, &again),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.prepare_starts, (size_t)2u);
        check_equal(probe.start_ids[0], "worker.1", sizeof("worker.1"));
        check_equal(probe.start_ids[1], "worker.2", sizeof("worker.2"));
        check_not_equal(probe.start_tokens[0], probe.start_tokens[1]);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("prepares parallel invoke idlocations in document order") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><parallel id='both'><state id='left'>"
            "<invoke idlocation='send_id'/></state><state id='right'>"
            "<invoke idlocation='nested.invoke_id'/></state></parallel>"
            "</scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 4u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 2u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_starts, (size_t)2u);
        check_equal(probe.start_ids[0], "left.1", sizeof("left.1"));
        check_equal(probe.start_ids[1], "right.2", sizeof("right.2"));
        check_equal(probe.start_commits, (size_t)2u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("starts invoke idlocation through the adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/></state></scxml>";
        const scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL,
            .prepare_start = invoke_idlocation_prepare_start,
            .prepare_cancel = invoke_idlocation_prepare_cancel,
            .close = invoke_idlocation_close,
            .is_quiescent = invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u};
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.invoke = &adapter;
        config.invoke_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_starts, (size_t)1u);
        check_equal(probe.start_ids[0], "worker.1", sizeof("worker.1"));
        check_equal(probe.start_commits, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("publishes the evaluated id after recoverable adapter rejection") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/><transition event='error.execution' "
            "cond='send_id == &quot;worker.1&quot;' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_invoke_stats invoke_stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 4u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        probe.start_status[0] = SCXML_ADAPTER_ERROR_EXECUTION;
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(probe.prepare_starts, (size_t)1u);
        check_equal(probe.start_ids[0], "worker.1", sizeof("worker.1"));
        check_equal(probe.start_commits, (size_t)0u);
        check_equal(probe.start_discards, (size_t)0u);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.start_failed, UINT64_C(1));
        check_equal(invoke_stats.active, (size_t)0u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("rolls back all dynamic starts when a later ticket is invalid") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/><invoke "
            "idlocation='nested.invoke_id'/></state></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 2u, .invoke = &adapter,
            .invoke_user = &probe};

        probe.invalid_start_ticket[1] = true;
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_HOOK_FAILED);
        check_null(session.impl);
        check_equal(probe.prepare_starts, (size_t)2u);
        check_equal(probe.start_commits, (size_t)0u);
        check_equal(probe.start_discards, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("preserves the prior location and raises error on assignment failure") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='failing_id'/><transition event='error.execution' "
            "cond='failing_id == &quot;old&quot;' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_public_data initial = {0};
        scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 4u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        initial.failing_id.size = sizeof("old") - 1u;
        memcpy(initial.failing_id.data, "old", sizeof("old"));
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(probe.prepare_starts, (size_t)0u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("does not prepare starts when invoke entry exhausts the effect journal") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/><invoke "
            "idlocation='nested.invoke_id'/></state></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 1u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 2u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_EFFECT_JOURNAL_FULL);
        check_null(session.impl);
        check_equal(probe.prepare_starts, (size_t)0u);
        check_equal(probe.start_commits, (size_t)0u);
        check_equal(probe.start_discards, (size_t)0u);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("uses the immutable row id for cancellation after location overwrite") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/><transition event='overwrite'>"
            "<assign location='send_id' expr='&quot;changed&quot;'/></transition>"
            "<transition event='leave' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view overwrite = {0};
        cflow_event_view leave = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(
            &program, "overwrite", 9u, &overwrite));
        check_true(scxml_program_event(&program, "leave", 5u, &leave));
        check_equal(scxml_session_try_send(&session, &overwrite),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_try_send(&session, &leave),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.prepare_cancels, (size_t)1u);
        check_equal(probe.cancel_tokens[0], probe.start_tokens[0]);
        check_equal(probe.cancel_ids[0], "worker.1", sizeof("worker.1"));
        check_equal(probe.cancel_commits, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("settles a prepared dynamic start before controlled cancellation") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='idle'><state id='idle'>"
            "<transition event='go' target='worker'/></state>"
            "<state id='worker'><invoke idlocation='send_id'/></state>"
            "</scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view go = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        probe.session = &session;
        probe.cancel_during_prepare = true;
        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(scxml_session_try_send(&session, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.cancelled);
        check_equal(probe.start_commits, (size_t)1u);
        check_equal(probe.start_discards, (size_t)0u);
        check_equal(probe.prepare_cancels, (size_t)1u);
        check_equal(probe.cancel_tokens[0], probe.start_tokens[0]);
        check_equal(probe.cancel_ids[0], "worker.1", sizeof("worker.1"));
        check_equal(probe.cancel_commits, (size_t)1u);
        check_equal(probe.cancel_discards, (size_t)0u);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("closes a dynamic invocation adapter exactly once at shutdown") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/></state></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        scxml_session_close(&session);
        scxml_session_close(&session);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("reports dynamic done identity through event name and invokeid") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='worker'><invoke "
            "idlocation='send_id'/><transition "
            "event='done.invoke.worker.invoke.1' "
            "cond='_event.name == &quot;done.invoke.worker.1&quot; "
            "&amp;&amp; _event.type == &quot;external&quot; "
            "&amp;&amp; _event.sendid == &quot;&quot; "
            "&amp;&amp; _event.origin == &quot;&quot; "
            "&amp;&amp; _event.origintype == &quot;&quot; "
            "&amp;&amp; _event.invokeid == &quot;worker.1&quot; "
            "&amp;&amp; _event.data == &quot;&quot;' "
            "target='done'/></state><final id='done'/></scxml>";
        const scxml_invoke_adapter adapter = {
            SCXML_ADAPTER_ABI, sizeof(adapter),
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
            invoke_idlocation_prepare_start,
            invoke_idlocation_prepare_cancel, NULL,
            invoke_idlocation_close, invoke_idlocation_quiescent};
        invoke_idlocation_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_invoke_stats invoke_stats = {0};
        const scxml_public_data initial = {0};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1, sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u, .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_report_invoke_done(&session, 0u),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_report_invoke_done(
                        &session, probe.start_tokens[0]),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_equal(probe.prepare_cancels, (size_t)0u);
        check_equal(scxml_session_report_invoke_done(
                        &session, probe.start_tokens[0]),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.returned_accepted, UINT64_C(1));
        check_equal(invoke_stats.returned_rejected, UINT64_C(1));
        check_equal(invoke_stats.completed, UINT64_C(1));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("transports scalar content on a literal external send") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer'>"
            "<content expr='&quot;payload&quot;'/></send>"
            "</onentry></state></scxml>";
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_PAYLOAD,
            .prepare_send = payload_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        payload_adapter_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u};
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.event_io = &event_io;
        config.adapter_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_equal(probe.kind, SCXML_PAYLOAD_CONTENT);
        check_equal(probe.content.kind, SCXML_PAYLOAD_VALUE_STRING);
        check_equal(probe.content_string, "payload", sizeof("payload"));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("transports bounded mixed XML content through the send adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer' id='later' delay='1ms'>"
            "<content> lead "
            "<p:item xmlns:p='urn:item'>x&lt;y</p:item><!--note-->"
            "</content></send></onentry></state></scxml>";
        static const char expected[] =
            " lead <p:item xmlns:p=\"urn:item\">x&lt;y</p:item><!--note-->";
        content_probe probe = {0};
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CONTENT,
            .prepare_send = content_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .delayed_send_capacity = 1u};
        scxml_event_io_adapter incomplete_event_io = event_io;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        incomplete_event_io.capabilities &=
            ~SCXML_EVENT_IO_CAP_CONTENT;
        config.event_io = &incomplete_event_io;
        config.adapter_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        config.event_io = &event_io;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.sends, (size_t)1u);
        check_equal(probe.kind, SCXML_CONTENT_XML_UTF8);
        check_equal(probe.bytes, expected, sizeof(expected));
        check_true(scxml_session_report_send_done(
            &session, "later", sizeof("later") - 1u));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("rolls back rejected text sends and invalid tickets") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'><onentry>"
            "<send event='out' target='peer'><content>plain text</content>"
            "</send></onentry><transition event='error.communication' "
            "target='done'/></state><final id='done'/></scxml>";
        const scxml_event_io_adapter event_io = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(event_io),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_CONTENT,
            .prepare_send = content_prepare_send,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        const scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        size_t index;

        for (index = 0u; index < 2u; ++index) {
            content_probe probe = {
                .send_status = index == 0u
                    ? SCXML_ADAPTER_FULL
                    : SCXML_ADAPTER_ACCEPTED,
                .invalid_send_ticket = index != 0u};
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            scxml_session session = {0};
            cflow_executor executor = {0};
            cflow_statechart_instance_stats stats = {0};
            const scxml_cmeta_session_options_v1 data = {
                SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
                sizeof(data), &initial};
            scxml_session_config config = {
                .program = &program, .executor = &executor,
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u};

            check_equal(compile_cmeta(source, &program, &diagnostic),
                        SCXML_OK);
            check_true(cflow_executor_serial_init(&executor));
            config.event_io = &event_io;
            config.adapter_user = &probe;
            check_equal(scxml_session_init_cmeta(
                            &session, &config, &data),
                        index == 0u
                            ? CFLOW_STATECHART_INSTANCE_OK
                            : CFLOW_STATECHART_INSTANCE_ACTION_FAILED);
            check_true(cflow_executor_wait_idle(&executor));
            check_equal(probe.sends, (size_t)1u);
            check_equal(probe.kind, SCXML_CONTENT_TEXT_UTF8);
            check_equal(probe.bytes, "plain text", sizeof("plain text"));
            check_equal(probe.commits, (size_t)0u);
            check_equal(probe.discards, (size_t)0u);
            if (index == 0u) {
                check_true(scxml_session_get_stats(&session, &stats));
                check_true(stats.done);
                check_equal(scxml_session_destroy(&session),
                            CFLOW_STATECHART_INSTANCE_OK);
            } else {
                check_null(session.impl);
            }
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("borrows structured CMeta content through the invoke adapter") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='armed'>"
            "<invoke id='worker' type='urn:test'>"
            "<content expr='nested'/></invoke>"
            "<transition event='finish' target='done'/></state>"
            "<final id='done'/></scxml>";
        const scxml_invoke_adapter invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(invoke),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_CONTENT,
            .prepare_start = content_prepare_start,
            .prepare_cancel = content_prepare_cancel,
            .close = dynamic_adapter_close,
            .is_quiescent = dynamic_adapter_quiescent};
        content_probe probe = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        scxml_public_data initial = {
            true, 0, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            sizeof(data), &initial};
        scxml_session_config config = {
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 4u,
            .completion_capacity = 2u, .microstep_limit = 16u,
            .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u};

        initial.nested.invoke_id.size = sizeof("snapshot") - 1u;
        memcpy(initial.nested.invoke_id.data, "snapshot",
               sizeof("snapshot"));
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.invoke = &invoke;
        config.invoke_user = &probe;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.starts, (size_t)1u);
        check_equal(probe.kind, SCXML_CONTENT_CMETA);
        check_true(probe.schema == &nested_data_desc);
        check_equal(probe.nested.invoke_id.size,
                    sizeof("snapshot") - 1u);
        check_equal(probe.nested.invoke_id.data, "snapshot",
                    sizeof("snapshot"));
        scxml_session_cancel(&session);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("keeps session identity unavailable in program-level bindings") {
        static const char name_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' name='Checkout'><state id='active'>"
            "<transition cond='_name == &quot;Checkout&quot;' target='done'/>"
            "</state><final id='done'/></scxml>";
        static const char session_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'>"
            "<transition cond='_sessionid != &quot;&quot;' target='done'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;
        cflow_statechart_instance_status init_status;
        cflow_statechart_instance_status destroy_status;
        const scxml_public_data initial = {
            false, 1, SCXML_PUBLIC_SOURCE_GOOD};

        check_equal(compile_cmeta(name_source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_direct_to_idle(
            &program, initial, &init_status, &destroy_status);
        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(destroy_status, CFLOW_STATECHART_INSTANCE_OK);
        scxml_program_destroy(&program);

        check_equal(compile_cmeta(session_source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_direct_to_idle(
            &program, initial, &init_status, &destroy_status);
        check_equal(init_status, CFLOW_STATECHART_INSTANCE_GUARD_FAILED);
        check_false(stats.done);
        check_false(stats.errored);
        check_equal(destroy_status, CFLOW_STATECHART_INSTANCE_OK);
        scxml_program_destroy(&program);
    }

    it("rolls back earlier assignments and raises error.execution") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='count' expr='2'/>"
            "<assign location='count' expr='source'/></onentry>"
            "<transition event='error.execution' cond='count == 1' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 1, SCXML_PUBLIC_SOURCE_FAIL});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("raises one runtime error for each protected system-variable write") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' name='machineName' initial='session'>"
            "<state id='session'><onentry>"
            "<assign location='_sessionid' expr='&quot;changed&quot;'/><assign "
            "location='count' expr='99'/></onentry>"
            "<transition event='error.execution' cond='count == 7 &amp;&amp; "
            "isBound(_sessionid) &amp;&amp; _sessionid != &quot;changed&quot;' "
            "target='currentEvent'/><transition event='error.execution' "
            "target='failed'/></state>"
            "<state id='currentEvent'><onentry>"
            "<assign location='_event' expr='true'/><assign location='count' "
            "expr='99'/></onentry><transition event='error.execution' "
            "cond='count == 7 &amp;&amp; isBound(_event) &amp;&amp; "
            "_event.name == &quot;error.execution&quot;' target='processors'/>"
            "<transition event='error.execution' target='failed'/></state>"
            "<state id='processors'><onentry>"
            "<assign location='_ioprocessors' expr='true'/><assign "
            "location='count' expr='99'/></onentry>"
            "<transition event='error.execution' cond='count == 7 &amp;&amp; "
            "isBound(_ioprocessors) &amp;&amp; "
            "_ioprocessors.scxml.location != &quot;&quot;' target='name'/>"
            "<transition event='error.execution' target='failed'/></state>"
            "<state id='name'><onentry>"
            "<assign location='_name' expr='&quot;changed&quot;'/><assign "
            "location='count' expr='99'/></onentry>"
            "<transition event='error.execution' cond='count == 7 &amp;&amp; "
            "isBound(_name) &amp;&amp; _name == &quot;machineName&quot;' "
            "target='passed'/><transition event='error.execution' "
            "target='failed'/></state>"
            "<state id='failed'/><final id='passed'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;
        scxml_status status;

        status = compile_cmeta(source, &program, &diagnostic);
        check_equal(status, SCXML_OK);
        if (status == SCXML_OK) {
            stats = run_to_idle(
                &program,
                (scxml_public_data){true, 7, SCXML_PUBLIC_SOURCE_GOOD});
            check_true(stats.done);
            check_false(stats.errored);
            scxml_program_destroy(&program);
        }
    }

    it("classifies processor error events as platform events") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='count' expr='source'/></onentry>"
            "<transition event='error.execution' "
            "cond='_event.name == &quot;error.execution&quot; &amp;&amp; "
            "_event.type == &quot;platform&quot; &amp;&amp; "
            "_event.sendid == &quot;&quot; &amp;&amp; "
            "_event.origin == &quot;&quot; &amp;&amp; "
            "_event.origintype == &quot;&quot; &amp;&amp; "
            "_event.invokeid == &quot;&quot; &amp;&amp; "
            "_event.data == &quot;&quot;' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 1, SCXML_PUBLIC_SOURCE_FAIL});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("applies early data initializers to a private session copy") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='early' initial='armed'>"
            "<datamodel><data id='enabled' expr='true'/>"
            "<data id='count' expr='2'/></datamodel>"
            "<state id='armed'><transition cond='enabled &amp;&amp; count == 2' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_public_data initial = {false, 9, SCXML_PUBLIC_SOURCE_GOOD};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(
                        &session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(initial.enabled);
        check_equal(initial.count, 9);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("admits late binding while rejecting unknown binding and external data") {
        static const char late[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late'><state id='only'/></scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='deferred'><state id='only'/></scxml>";
        static const char external[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late'><datamodel>"
            "<data id='count' src='values.json'/></datamodel>"
            "<state id='only'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_cmeta(late, &program, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
        check_equal(compile_cmeta(unknown, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(compile_cmeta(external, &program, &diagnostic),
                    SCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("initializes late root and nested data before onentry and initial work") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='parent'>"
            "<datamodel><data id='count' expr='2'/></datamodel>"
            "<state id='parent'>"
            "<datamodel><data id='enabled' expr='count == 2'/></datamodel>"
            "<onentry><if cond='enabled'><assign location='total' expr='2'/>"
            "<else/><assign location='total' expr='9'/></if></onentry>"
            "<initial><transition target='leaf'>"
            "<assign location='ratio' expr='total'/></transition></initial>"
            "<state id='leaf'><transition "
            "cond='enabled &amp;&amp; count == 2 &amp;&amp; total == 2 "
            "&amp;&amp; ratio == 2' target='done'/></state>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        uint32_t requirements = 0u;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(
            &program, &requirements));
        check_true((requirements &
                    SCXML_REQUIREMENT_LATE_BINDING) != 0u);
        check_false(scxml_program_instance_bindings(
            &program, &bindings, &binding_count));
        stats = run_to_idle(
            &program,
            (scxml_public_data){false, 9, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("reads the caller CMeta state before a late declaration is initialized") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='before'>"
            "<state id='before'><transition cond='count == 9' "
            "target='leaf'/></state>"
            "<state id='leaf'><datamodel>"
            "<data id='count' expr='2'/></datamodel>"
            "<transition cond='count == 2' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){false, 9, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("initializes late state data exactly once across reentry") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='active'>"
            "<state id='active'><datamodel>"
            "<data id='count' expr='2'/></datamodel>"
            "<transition event='leave' target='away'/>"
            "<transition event='finish' cond='count == 5' target='done'/>"
            "</state><state id='away'><onentry>"
            "<assign location='count' expr='5'/></onentry>"
            "<transition event='return' target='active'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view leave = {0};
        cflow_event_view return_event = {0};
        cflow_event_view finish = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            false, 9, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 3u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 1u};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "leave", 5u, &leave));
        check_true(scxml_program_event(
            &program, "return", 6u, &return_event));
        check_true(scxml_program_event(&program, "finish", 6u, &finish));
        check_equal(scxml_session_try_send(&session, &leave),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_try_send(&session, &return_event),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("initializes late parallel regions in deterministic document order") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='both'>"
            "<parallel id='both'><transition "
            "cond='enabled &amp;&amp; total == 2' target='done'/>"
            "<state id='left'><datamodel>"
            "<data id='count' expr='2'/></datamodel>"
            "<onentry><assign location='enabled' expr='count == 2'/>"
            "</onentry></state>"
            "<state id='right'><datamodel>"
            "<data id='total' expr='count'/></datamodel></state>"
            "</parallel><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){false, 9, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("preserves late data through history restoration") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='parent'>"
            "<state id='parent' initial='leaf'>"
            "<history id='saved'><transition target='leaf'/></history>"
            "<state id='leaf'><datamodel>"
            "<data id='count' expr='2'/></datamodel>"
            "<transition event='leave' target='away'>"
            "<assign location='count' expr='5'/></transition>"
            "<transition event='finish' cond='count == 5' target='done'/>"
            "</state></state>"
            "<state id='away'><transition event='return' target='saved'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view leave = {0};
        cflow_event_view return_event = {0};
        cflow_event_view finish = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_public_data initial = {
            false, 9, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 3u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 1u};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "leave", 5u, &leave));
        check_true(scxml_program_event(
            &program, "return", 6u, &return_event));
        check_true(scxml_program_event(&program, "finish", 6u, &finish));
        check_equal(scxml_session_try_send(&session, &leave),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_try_send(&session, &return_event),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("initializes late parent data before first history default work") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='before'>"
            "<state id='before'><transition target='saved'/></state>"
            "<state id='parent' initial='leaf'><datamodel>"
            "<data id='count' expr='2'/></datamodel>"
            "<history id='saved'><transition target='leaf'>"
            "<assign location='total' expr='count'/></transition></history>"
            "<state id='leaf'><transition cond='total == 2' "
            "target='done'/></state></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){false, 9, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("fails late initialization atomically and requires journal capacity") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' binding='late' initial='active'>"
            "<datamodel><data id='enabled' expr='true'/></datamodel>"
            "<state id='active'><datamodel>"
            "<data id='count' expr='2'/><data id='count' expr='source'/>"
            "</datamodel></state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_public_data initial = {
            false, 9, SCXML_PUBLIC_SOURCE_FAIL};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 2u,
            .completion_capacity = 1u,
            .microstep_limit = 16u,
            .effect_capacity = 0u};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        config.effect_capacity = 1u;
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_ACTION_FAILED);
        check_null(session.impl);
        check_false(initial.enabled);
        check_equal(initial.count, 9);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("binds scalar donedata to the parent completion event") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='parent'>"
            "<state id='parent' initial='work'>"
            "<state id='work'><transition target='childDone'/></state>"
            "<final id='childDone'><donedata>"
            "<content expr='count'/></donedata></final>"
            "<transition event='done.state.*' "
            "cond='_event.name == &quot;done.state.parent&quot; &amp;&amp; "
            "_event.data == &quot;7&quot; &amp;&amp; "
            "_event.type == &quot;internal&quot; &amp;&amp; "
            "_event.sendid == &quot;&quot; &amp;&amp; "
            "_event.origin == &quot;&quot; &amp;&amp; "
            "_event.origintype == &quot;&quot; &amp;&amp; "
            "_event.invokeid == &quot;&quot;' target='success'/></state>"
            "<final id='success'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;
        uint64_t matching_microsteps;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 7, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        matching_microsteps = stats.microsteps;
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 6, SCXML_PUBLIC_SOURCE_GOOD});
        check_greater(matching_microsteps, stats.microsteps);
        scxml_program_destroy(&program);
    }

    it("orders failed donedata content before empty completion data") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='parent'>"
            "<state id='parent' initial='work'>"
            "<transition event='error.execution' target='waiting'/>"
            "<transition event='done.state.parent' target='failed'/>"
            "<state id='work'><transition target='childDone'/></state>"
            "<final id='childDone'><donedata>"
            "<content expr='_event.data.count'/></donedata></final>"
            "</state><state id='waiting'>"
            "<transition event='done.state.parent' "
            "cond='_event.data == &quot;&quot;' target='success'/>"
            "<transition event='*' target='failed'/></state>"
            "<final id='success'/><state id='failed'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 7, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        check_false(stats.errored);
        scxml_program_destroy(&program);
    }

    it("keeps raw StateChart execution independent of donedata envelopes") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='parent'>"
            "<state id='parent' initial='childDone'>"
            "<final id='childDone'><donedata>"
            "<content expr='count'/></donedata></final>"
            "<transition event='done.state.parent' target='success'/>"
            "</state><final id='success'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_status init_status;
        cflow_statechart_instance_status destroy_status;
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_direct_to_idle(
            &program,
            (scxml_public_data){true, 7, SCXML_PUBLIC_SOURCE_GOOD},
            &init_status, &destroy_status);
        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(destroy_status, CFLOW_STATECHART_INSTANCE_OK);
        scxml_program_destroy(&program);
    }

    it("rejects completion payload capacity arithmetic overflow") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='parent'>"
            "<state id='parent' initial='done'>"
            "<final id='done'><donedata>"
            "<content expr='count'/></donedata></final>"
            "</state></scxml>";
        const scxml_public_data initial = {
            true, 7, SCXML_PUBLIC_SOURCE_GOOD};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        const scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 2u,
            .completion_capacity = SIZE_MAX,
            .microstep_limit = 16u};

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED);
        check_null(session.impl);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("materializes every donedata param from the same state snapshot") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='parent'>"
            "<state id='parent' initial='work'>"
            "<state id='work'><transition target='childDone'/></state>"
            "<final id='childDone'><donedata>"
            "<param name='count' expr='1'/>"
            "<param name='enabled' expr='count == 7'/>"
            "</donedata></final>"
            "<transition event='done.state.parent' "
            "cond='_event.data.count == 1 &amp;&amp; "
            "_event.data.enabled == true' target='success'/></state>"
            "<final id='success'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){false, 7, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        scxml_program_destroy(&program);
    }

    it("discards failed donedata params and processes error.execution") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='idle'>"
            "<state id='idle'><transition event='go' target='running'/>"
            "</state><parallel id='running'>"
            "<state id='payload' initial='work'>"
            "<state id='work'><transition target='childDone'/></state>"
            "<final id='childDone'><donedata>"
            "<param name='enabled' expr='true'/>"
            "<param name='count' expr='source'/>"
            "</donedata></final>"
            "</state>"
            "<state id='hold'/><transition event='error.execution' "
            "cond='enabled == false &amp;&amp; count == 7 &amp;&amp; "
            "_event.name == &quot;error.execution&quot; &amp;&amp; "
            "_event.type == &quot;platform&quot; &amp;&amp; "
            "_event.data == &quot;&quot;' target='success'/>"
            "</parallel><final id='success'/></scxml>";
        const scxml_public_data initial = {
            false, 7, SCXML_PUBLIC_SOURCE_FAIL};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_event_view go = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 2u,
            .completion_capacity = 4u,
            .microstep_limit = 16u};
        size_t copies, moves, destroys;
        size_t copies_before, destroys_before, live_before;

        atomic_store_explicit(
            &public_data_copy_count, 0u, memory_order_relaxed);
        atomic_store_explicit(
            &public_data_move_count, 0u, memory_order_relaxed);
        atomic_store_explicit(
            &public_data_destroy_count, 0u, memory_order_relaxed);
        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init_cmeta(&session, &config, &data),
                    CFLOW_STATECHART_INSTANCE_OK);
        copies = atomic_load_explicit(
            &public_data_copy_count, memory_order_relaxed);
        moves = atomic_load_explicit(
            &public_data_move_count, memory_order_relaxed);
        destroys = atomic_load_explicit(
            &public_data_destroy_count, memory_order_relaxed);
        check_true(copies + moves > destroys);
        copies_before = copies;
        destroys_before = destroys;
        live_before = copies + moves - destroys;

        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(scxml_session_try_send(&session, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        copies = atomic_load_explicit(
            &public_data_copy_count, memory_order_relaxed);
        moves = atomic_load_explicit(
            &public_data_move_count, memory_order_relaxed);
        destroys = atomic_load_explicit(
            &public_data_destroy_count, memory_order_relaxed);
        check_true(copies > copies_before);
        check_true(destroys > destroys_before);
        check_true(copies + moves >= destroys);
        check_true(copies + moves - destroys <= live_before);

        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        copies = atomic_load_explicit(
            &public_data_copy_count, memory_order_relaxed);
        moves = atomic_load_explicit(
            &public_data_move_count, memory_order_relaxed);
        destroys = atomic_load_explicit(
            &public_data_destroy_count, memory_order_relaxed);
        check_equal(copies + moves, destroys);
        scxml_program_destroy(&program);
    }

    it("binds inline XML donedata to the parent completion event") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta' initial='parent'>"
            "<state id='parent' initial='work'>"
            "<state id='work'><transition target='childDone'/></state>"
            "<final id='childDone'><donedata><content>"
            "<p:value xmlns:p='urn:test'>ready</p:value>"
            "</content></donedata></final>"
            "<transition event='done.state.*' "
            "cond='_event.data != &quot;&quot;' target='success'/></state>"
            "<final id='success'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_statechart_instance_stats stats;

        check_equal(compile_cmeta(source, &program, &diagnostic),
                    SCXML_OK);
        stats = run_to_idle(
            &program,
            (scxml_public_data){true, 7, SCXML_PUBLIC_SOURCE_GOOD});
        check_true(stats.done);
        scxml_program_destroy(&program);
    }

    it("rejects invalid donedata structure and unknown param fields") {
        static const char wrong_parent[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='only'><donedata>"
            "<content expr='count'/></donedata></state></scxml>";
        static const char conflicting_content[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><final id='done'><donedata>"
            "<content expr='count'>text</content>"
            "</donedata></final></scxml>";
        static const char mixed_payload[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><final id='done'><donedata>"
            "<param name='count' expr='1'/><content>text</content>"
            "</donedata></final></scxml>";
        static const char empty_payload[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><final id='done'><donedata/>"
            "</final></scxml>";
        static const char unknown_param[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><final id='done'><donedata>"
            "<param name='missing' expr='1'/>"
            "</donedata></final></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_cmeta(wrong_parent, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(compile_cmeta(conflicting_content, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(compile_cmeta(mixed_payload, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(compile_cmeta(empty_payload, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(compile_cmeta(unknown_param, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("admits protected system locations and rejects unknown assignments") {
        static const char missing_location[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign expr='2'/></onentry></state></scxml>";
        static const char missing_expr[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='count'/></onentry></state></scxml>";
        static const char unknown_location[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='missing' expr='2'/></onentry></state></scxml>";
        static const char unknown_system_location[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='_missing' expr='2'/></onentry></state></scxml>";
        static const char scalar_system_subpath[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='_name.value' expr='2'/></onentry>"
            "</state></scxml>";
        static const char malformed_system_path[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='active'><onentry>"
            "<assign location='_event..name' expr='2'/></onentry>"
            "</state></scxml>";
        static const char null_assignment[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<assign location='count' expr='2'/></onentry></state></scxml>";
        const char *invalid[] = {
            missing_location, missing_expr, unknown_location,
            unknown_system_location, scalar_system_subpath,
            malformed_system_path};
        static const char *read_only_system_locations[] = {
            "_sessionid", "_name", "_event", "_event.name",
            "_event.data.count", "_event.unknown", "_ioprocessors",
            "_ioprocessors.scxml.location", "_ioprocessors.unknown"};
        enum { ASSIGNMENT_SOURCE_CAPACITY = 256u };
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_cmeta(invalid[index], &program, &diagnostic),
                        SCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }
        for (index = 0u;
             index < sizeof(read_only_system_locations) /
                         sizeof(read_only_system_locations[0]);
             ++index) {
            char source[ASSIGNMENT_SOURCE_CAPACITY];
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            int written = snprintf(
                source, sizeof(source),
                "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
                "datamodel='cmeta'><state id='active'><onentry>"
                "<assign location='%s' expr='2'/></onentry></state></scxml>",
                read_only_system_locations[index]);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(compile_cmeta(source, &program, &diagnostic),
                        SCXML_OK);
            scxml_program_destroy(&program);
        }
        {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(scxml_compile(
                            &program, null_assignment,
                            strlen(null_assignment), NULL, &diagnostic),
                        SCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }
    }
}
