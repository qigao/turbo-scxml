#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_TEXT_CAPACITY = 95u };

typedef struct test_text {
    size_t size;
    char data[TEST_TEXT_CAPACITY + 1u];
} test_text;

typedef struct ccxml_foreach_record {
    test_text destination;
    int code;
} ccxml_foreach_record;

typedef struct ccxml_foreach_managed {
    int *resource;
} ccxml_foreach_managed;

#define CMETA_USER_TYPE_LIST                                                \
    , (O, ccxml_foreach_record, cmeta_type_ccxml_foreach_record,             \
       CMETA_T_OBJECT, cmeta_traits_ccxml_foreach_record)                    \
    , (O, ccxml_foreach_managed, cmeta_type_ccxml_foreach_managed,           \
       CMETA_T_OBJECT, cmeta_traits_ccxml_foreach_managed)
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST

#include <ccxml/ccxml.h>
#include <cstl/typed.h>

#include "ccxml_internal.h"
#include "tinytest.h"

Struct(test_conference,
    (test_text, id)
);

Struct(test_dialog,
    (test_text, id)
);

Struct(test_state,
    (test_conference, conference),
    (test_dialog, dialog),
    (test_text, read_only),
    (test_text, mode),
    (int, count)
);

Struct(malformed_payload_state,
    (unsigned char, bad)
);

Struct(foreach_state,
    (TYPE(Vec, int), values),
    (int, existing),
    (size_t, root_index)
);

Struct(foreach_record_state,
    (TYPE(Vec, ccxml_foreach_record), values)
);

Struct(foreach_managed_state,
    (TYPE(Vec, ccxml_foreach_managed), values)
);

static bool foreach_record_copy(void *destination_, const void *source_) {
    ccxml_foreach_record *destination =
        (ccxml_foreach_record *)destination_;
    const ccxml_foreach_record *source =
        (const ccxml_foreach_record *)source_;
    if (destination == NULL || source == NULL) return false;
    *destination = *source;
    return true;
}

static void foreach_record_move(void *destination_, void *source_) {
    ccxml_foreach_record *destination =
        (ccxml_foreach_record *)destination_;
    ccxml_foreach_record *source = (ccxml_foreach_record *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
}

static void foreach_record_destroy(void *object_) {
    (void)object_;
}

const cmeta_type_traits cmeta_traits_ccxml_foreach_record = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    .copy_construct = foreach_record_copy,
    .move_construct = foreach_record_move,
    .destroy = foreach_record_destroy};

static bool foreach_managed_copy(void *destination_, const void *source_) {
    ccxml_foreach_managed *destination =
        (ccxml_foreach_managed *)destination_;
    const ccxml_foreach_managed *source =
        (const ccxml_foreach_managed *)source_;
    if (destination == NULL || source == NULL) return false;
    destination->resource = NULL;
    if (source->resource == NULL) return true;
    destination->resource = (int *)malloc(sizeof(*destination->resource));
    if (destination->resource == NULL) return false;
    *destination->resource = *source->resource;
    return true;
}

static void foreach_managed_move(void *destination_, void *source_) {
    ccxml_foreach_managed *destination =
        (ccxml_foreach_managed *)destination_;
    ccxml_foreach_managed *source = (ccxml_foreach_managed *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    source->resource = NULL;
}

static void foreach_managed_destroy(void *object_) {
    ccxml_foreach_managed *object = (ccxml_foreach_managed *)object_;
    if (object == NULL) return;
    free(object->resource);
    object->resource = NULL;
}

const cmeta_type_traits cmeta_traits_ccxml_foreach_managed = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = foreach_managed_copy,
    .move_construct = foreach_managed_move,
    .destroy = foreach_managed_destroy};

static const cmeta_type_identity foreach_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.ccxml.foreach.record.type");

const cmeta_type_desc cmeta_type_ccxml_foreach_record = {
    .name = "ccxml_foreach_record",
    .size = sizeof(ccxml_foreach_record),
    .align = _Alignof(ccxml_foreach_record),
    .kind = CMETA_T_OBJECT,
    .traits = &cmeta_traits_ccxml_foreach_record,
    .identity = &foreach_record_identity};

const cmeta_type_desc cmeta_type_ccxml_foreach_record_ptr = {
    .name = "ccxml_foreach_record *",
    .size = sizeof(ccxml_foreach_record *),
    .align = _Alignof(ccxml_foreach_record *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_type_ccxml_foreach_record};

static const cmeta_type_identity foreach_managed_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.ccxml.foreach.managed.type");

const cmeta_type_desc cmeta_type_ccxml_foreach_managed = {
    .name = "ccxml_foreach_managed",
    .size = sizeof(ccxml_foreach_managed),
    .align = _Alignof(ccxml_foreach_managed),
    .kind = CMETA_T_OBJECT,
    .traits = &cmeta_traits_ccxml_foreach_managed,
    .identity = &foreach_managed_identity};

const cmeta_type_desc cmeta_type_ccxml_foreach_managed_ptr = {
    .name = "ccxml_foreach_managed *",
    .size = sizeof(ccxml_foreach_managed *),
    .align = _Alignof(ccxml_foreach_managed *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_type_ccxml_foreach_managed};

static void text_move(void *destination, void *source) {
    memcpy(destination, source, sizeof(test_text));
    memset(source, 0, sizeof(test_text));
}

static void text_destroy(void *object) {
    memset(object, 0, sizeof(test_text));
}

static const cmeta_type_traits text_traits = {
    .flags = CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .move_construct = text_move,
    .destroy = text_destroy};

static const cmeta_type_desc text_type = {
    .name = "ccxml_test_text",
    .size = sizeof(test_text),
    .align = _Alignof(test_text),
    .kind = CMETA_T_OBJECT,
    .traits = &text_traits};

static bool text_is_zero(const void *object) {
    return object != NULL && ((const test_text *)object)->size == 0u;
}

static cmeta_status text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    test_text *text = (test_text *)object;
    if (text == NULL || (data == NULL && size != 0u))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > TEST_TEXT_CAPACITY)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) memcpy(text->data, data, size);
    text->data[size] = '\0';
    text->size = size;
    return CMETA_OK;
}

static void text_restore_zero(void *object) {
    if (object != NULL) memset(object, 0, sizeof(test_text));
}

static cmeta_status text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const test_text *text = (const test_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        text->size > TEST_TEXT_CAPACITY)
        return CMETA_INVALID_ARGUMENT;
    *out_data = (const unsigned char *)text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_data_buffer_shape text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED};

static const cmeta_data_buffer_ops text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = text_is_zero,
    .assign = text_assign,
    .restore_zero = text_restore_zero,
    .read = text_read};

static const cmeta_data_desc text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.text",
    .display_name = "CCXML test text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &text_type,
    .shape = &text_shape,
    .buffer_ops = &text_ops};

static const cmeta_data_buffer_shape borrowed_text_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED};

static const cmeta_data_buffer_ops borrowed_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &text_type,
    .ownership = CMETA_DATA_BUFFER_BORROWED,
    .is_zero = text_is_zero,
    .assign = text_assign,
    .restore_zero = text_restore_zero,
    .read = text_read};

static const cmeta_data_desc borrowed_text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.borrowed-text",
    .display_name = "CCXML borrowed test text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &text_type,
    .shape = &borrowed_text_shape,
    .buffer_ops = &borrowed_text_ops};

static const cmeta_type_traits aggregate_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};

static const cmeta_type_desc conference_type = {
    .name = "ccxml_test_conference",
    .size = sizeof(test_conference),
    .align = _Alignof(test_conference),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc conference_fields[] = {
    {"test.ccxml.conference.id", "id",
     offsetof(test_conference, id), &text_desc}};

static const cmeta_data_struct_shape conference_shape = {
    .layout = StructMeta(test_conference),
    .fields = conference_fields,
    .field_count = sizeof(conference_fields) / sizeof(conference_fields[0])};

static const cmeta_data_desc conference_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.conference",
    .display_name = "CCXML test conference",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &conference_type,
    .shape = &conference_shape};

static const cmeta_type_desc dialog_type = {
    .name = "ccxml_test_dialog",
    .size = sizeof(test_dialog),
    .align = _Alignof(test_dialog),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc dialog_fields[] = {
    {"test.ccxml.dialog.id", "id",
     offsetof(test_dialog, id), &text_desc}};

static const cmeta_data_struct_shape dialog_shape = {
    .layout = StructMeta(test_dialog),
    .fields = dialog_fields,
    .field_count = sizeof(dialog_fields) / sizeof(dialog_fields[0])};

static const cmeta_data_desc dialog_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.dialog",
    .display_name = "CCXML test dialog",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &dialog_type,
    .shape = &dialog_shape};

static const cmeta_type_desc state_type = {
    .name = "ccxml_test_state",
    .size = sizeof(test_state),
    .align = _Alignof(test_state),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc state_fields[] = {
    {"test.ccxml.state.conference", "conference",
     offsetof(test_state, conference), &conference_desc},
    {"test.ccxml.state.dialog", "dialog",
     offsetof(test_state, dialog), &dialog_desc},
    {"test.ccxml.state.read-only", "read_only",
     offsetof(test_state, read_only), &borrowed_text_desc},
    {"test.ccxml.state.mode", "mode",
     offsetof(test_state, mode), &text_desc},
    {"test.ccxml.state.count", "count",
     offsetof(test_state, count), &cmeta_data_int}};

static const cmeta_data_struct_shape state_shape = {
    .layout = StructMeta(test_state),
    .fields = state_fields,
    .field_count = sizeof(state_fields) / sizeof(state_fields[0])};

static const cmeta_data_desc state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.state",
    .display_name = "CCXML test state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &state_type,
    .shape = &state_shape};

static const cmeta_type_desc malformed_byte_type = {
    .name = "ccxml_malformed_payload_byte",
    .size = sizeof(unsigned char),
    .align = _Alignof(unsigned char),
    .kind = CMETA_T_INTEGER,
    .traits = &aggregate_traits};

static const cmeta_data_integer_shape malformed_integer_shape = {
    .bits = 64u};

static const cmeta_data_desc malformed_integer_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.malformed-integer",
    .display_name = "CCXML malformed integer",
    .kind = CMETA_DATA_SINT,
    .storage_type = &malformed_byte_type,
    .shape = &malformed_integer_shape};

static const cmeta_type_desc malformed_state_type = {
    .name = "ccxml_malformed_payload_state",
    .size = sizeof(malformed_payload_state),
    .align = _Alignof(malformed_payload_state),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc malformed_state_fields[] = {
    {"test.ccxml.malformed-state.bad", "bad",
     offsetof(malformed_payload_state, bad), &malformed_integer_desc}};

static const cmeta_data_struct_shape malformed_state_shape = {
    .layout = StructMeta(malformed_payload_state),
    .fields = malformed_state_fields,
    .field_count = sizeof(malformed_state_fields) /
        sizeof(malformed_state_fields[0])};

static const cmeta_data_desc malformed_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.malformed-state",
    .display_name = "CCXML malformed payload state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &malformed_state_type,
    .shape = &malformed_state_shape};

static const cmeta_type_desc foreach_state_type = {
    .name = "ccxml_foreach_state",
    .size = sizeof(foreach_state),
    .align = _Alignof(foreach_state),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc foreach_state_fields[] = {
    {"test.ccxml.foreach.values", "values",
     offsetof(foreach_state, values), &cmeta_data_sequence},
    {"test.ccxml.foreach.existing", "existing",
     offsetof(foreach_state, existing), &cmeta_data_int},
    {"test.ccxml.foreach.root-index", "root_index",
     offsetof(foreach_state, root_index), &cmeta_data_size}};

static const cmeta_data_struct_shape foreach_state_shape = {
    .layout = StructMeta(foreach_state),
    .fields = foreach_state_fields,
    .field_count = sizeof(foreach_state_fields) /
        sizeof(foreach_state_fields[0])};

static const cmeta_data_desc foreach_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.foreach.state",
    .display_name = "CCXML foreach state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &foreach_state_type,
    .shape = &foreach_state_shape};

static const cmeta_type_desc foreach_record_state_type = {
    .name = "ccxml_foreach_record_state",
    .size = sizeof(foreach_record_state),
    .align = _Alignof(foreach_record_state),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc foreach_record_state_fields[] = {
    {"test.ccxml.foreach.records", "values",
     offsetof(foreach_record_state, values), &cmeta_data_sequence}};

static const cmeta_data_struct_shape foreach_record_state_shape = {
    .layout = StructMeta(foreach_record_state),
    .fields = foreach_record_state_fields,
    .field_count = sizeof(foreach_record_state_fields) /
        sizeof(foreach_record_state_fields[0])};

static const cmeta_data_desc foreach_record_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.foreach.record-state",
    .display_name = "CCXML foreach record state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &foreach_record_state_type,
    .shape = &foreach_record_state_shape};

static const cmeta_field_desc foreach_record_layout_fields[] = {
    {"destination", "test_text",
     offsetof(ccxml_foreach_record, destination), sizeof(test_text),
     _Alignof(test_text), &text_type, NULL},
    {"code", "int", offsetof(ccxml_foreach_record, code), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}};

static const cmeta_struct_desc foreach_record_layout = {
    .name = "ccxml_foreach_record",
    .size = sizeof(ccxml_foreach_record),
    .align = _Alignof(ccxml_foreach_record),
    .fields = foreach_record_layout_fields,
    .field_count = sizeof(foreach_record_layout_fields) /
        sizeof(foreach_record_layout_fields[0])};

static const cmeta_data_field_desc foreach_record_data_fields[] = {
    {"test.ccxml.foreach.record.destination", "destination",
     offsetof(ccxml_foreach_record, destination), &text_desc},
    {"test.ccxml.foreach.record.code", "code",
     offsetof(ccxml_foreach_record, code), &cmeta_data_int}};

static const cmeta_data_struct_shape foreach_record_data_shape = {
    .layout = &foreach_record_layout,
    .fields = foreach_record_data_fields,
    .field_count = sizeof(foreach_record_data_fields) /
        sizeof(foreach_record_data_fields[0])};

static const cmeta_data_desc foreach_record_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.foreach.record",
    .display_name = "CCXML foreach record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cmeta_type_ccxml_foreach_record,
    .shape = &foreach_record_data_shape};

static const cmeta_type_identity foreach_record_peer_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.ccxml.foreach.record.type");

static const cmeta_type_desc foreach_record_peer_type = {
    .name = "ccxml_foreach_record_peer",
    .size = sizeof(ccxml_foreach_record),
    .align = _Alignof(ccxml_foreach_record),
    .kind = CMETA_T_OBJECT,
    .traits = &cmeta_traits_ccxml_foreach_record,
    .identity = &foreach_record_peer_identity};

static const cmeta_data_desc foreach_record_alias_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.foreach.record-alias",
    .display_name = "CCXML foreach record alias",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &foreach_record_peer_type,
    .shape = &foreach_record_data_shape};

static const cmeta_data_integer_shape foreach_int_override_shape = {
    .bits = (uint8_t)(sizeof(int) * CHAR_BIT)};

static const cmeta_data_desc foreach_int_override_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.foreach.int-override",
    .display_name = "CCXML foreach int override",
    .kind = CMETA_DATA_SINT,
    .storage_type = &cmeta_type_int,
    .shape = &foreach_int_override_shape};

static const cmeta_type_desc foreach_managed_state_type = {
    .name = "ccxml_foreach_managed_state",
    .size = sizeof(foreach_managed_state),
    .align = _Alignof(foreach_managed_state),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc foreach_managed_state_fields[] = {
    {"test.ccxml.foreach.managed-values", "values",
     offsetof(foreach_managed_state, values), &cmeta_data_sequence}};

static const cmeta_data_struct_shape foreach_managed_state_shape = {
    .layout = StructMeta(foreach_managed_state),
    .fields = foreach_managed_state_fields,
    .field_count = sizeof(foreach_managed_state_fields) /
        sizeof(foreach_managed_state_fields[0])};

static const cmeta_data_desc foreach_managed_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.foreach.managed-state",
    .display_name = "CCXML foreach managed state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &foreach_managed_state_type,
    .shape = &foreach_managed_state_shape};

typedef struct foreach_provider_probe foreach_provider_probe;

typedef struct foreach_provider_ticket {
    foreach_provider_probe *owner;
    bool live;
} foreach_provider_ticket;

struct foreach_provider_probe {
    foreach_provider_ticket tickets[4];
    size_t prepare_count;
    size_t commit_count;
    size_t discard_count;
    size_t reject_on_prepare;
    size_t close_count;
    char destinations[4][TEST_TEXT_CAPACITY + 1u];
    size_t destination_sizes[4];
};

static void foreach_ticket_commit(void *user) {
    foreach_provider_ticket *ticket = (foreach_provider_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ++ticket->owner->commit_count;
}

static void foreach_ticket_discard(void *user) {
    foreach_provider_ticket *ticket = (foreach_provider_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ++ticket->owner->discard_count;
}

static scxml_adapter_status foreach_prepare_accept(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    foreach_provider_probe *probe = (foreach_provider_probe *)user;
    foreach_provider_ticket *ticket;
    if (out_error != NULL) *out_error = NULL;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        request->connection_id == NULL || request->connection_id_size == 0u ||
        probe->prepare_count >= sizeof(probe->tickets) /
            sizeof(probe->tickets[0]))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->prepare_count;
    if (probe->reject_on_prepare == probe->prepare_count)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    ticket = &probe->tickets[probe->prepare_count - 1u];
    *ticket = (foreach_provider_ticket){.owner = probe, .live = true};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = foreach_ticket_commit,
        .discard = foreach_ticket_discard,
        .user = ticket};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status foreach_prepare_create_call(
    void *user, const ccxml_create_call_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    foreach_provider_probe *probe = (foreach_provider_probe *)user;
    foreach_provider_ticket *ticket;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        request->destination == NULL || request->destination_size == 0u ||
        request->destination_size > TEST_TEXT_CAPACITY ||
        probe->prepare_count >= sizeof(probe->tickets) /
            sizeof(probe->tickets[0]))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    index = probe->prepare_count++;
    probe->destination_sizes[index] = request->destination_size;
    memcpy(
        probe->destinations[index], request->destination,
        request->destination_size);
    probe->destinations[index][request->destination_size] = '\0';
    if (probe->reject_on_prepare == probe->prepare_count)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    ticket = &probe->tickets[index];
    *ticket = (foreach_provider_ticket){.owner = probe, .live = true};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = foreach_ticket_commit,
        .discard = foreach_ticket_discard,
        .user = ticket};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status foreach_prepare_redirect(
    void *user, const ccxml_redirect_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    ccxml_create_call_request destination;
    if (request == NULL || request->connection_id == NULL ||
        request->connection_id_size == 0u)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    destination = (ccxml_create_call_request){
        .destination = request->destination,
        .destination_size = request->destination_size};
    return foreach_prepare_create_call(
        user, &destination, out_ticket, out_error);
}

static void foreach_provider_close(void *user) {
    ++((foreach_provider_probe *)user)->close_count;
}

static bool foreach_provider_quiescent(void *user) {
    (void)user;
    return true;
}

static const ccxml_telephony_adapter_v1 foreach_provider_adapter = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = foreach_prepare_accept,
    .close = foreach_provider_close,
    .is_quiescent = foreach_provider_quiescent,
    .prepare_create_call = foreach_prepare_create_call,
    .prepare_redirect = foreach_prepare_redirect};

typedef struct conference_provider_probe {
    bool live;
    size_t commit_count;
    size_t discard_count;
    size_t close_count;
    char destroyed_conference_id[32];
    size_t destroyed_conference_id_size;
    char dialog_source[32];
    char dialog_connection_id[32];
    char dialog_media_type[32];
    char started_prepared_dialog_id[32];
    char terminated_dialog_id[32];
    bool terminate_immediate;
    const test_text *published_dialog_id;
    const char *expected_dialog_id;
    size_t expected_dialog_id_size;
    bool dialog_id_visible_at_commit;
} conference_provider_probe;

typedef struct send_cancel_probe {
    bool live;
    size_t commit_count;
    size_t discard_count;
    size_t close_count;
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t send_id_size;
    char cancel_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t cancel_id_size;
    size_t payload_count;
    char payload_names[3][32];
    scxml_payload_value payload_values[3];
    char payload_string[32];
    const cmeta_data_desc *payload_schema;
    const void *payload_object;
} send_cancel_probe;

static void send_cancel_commit(void *user) {
    send_cancel_probe *probe = (send_cancel_probe *)user;
    if (probe == NULL || !probe->live) return;
    probe->live = false;
    ++probe->commit_count;
}

static void send_cancel_discard(void *user) {
    send_cancel_probe *probe = (send_cancel_probe *)user;
    if (probe == NULL || !probe->live) return;
    probe->live = false;
    ++probe->discard_count;
}

static scxml_adapter_status cmeta_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    send_cancel_probe *probe = (send_cancel_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        request->id == NULL || request->id_size == 0u ||
        request->id_size > SCXML_EVENT_METADATA_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->send_id, request->id, request->id_size);
    probe->send_id[request->id_size] = '\0';
    probe->send_id_size = request->id_size;
    probe->payload_count = request->payload.entry_count;
    if (request->payload.entry_count <= 3u) {
        size_t index;
        for (index = 0u; index < request->payload.entry_count; ++index) {
            const scxml_payload_entry *entry =
                &request->payload.entries[index];
            if (entry->name_size < sizeof(probe->payload_names[index])) {
                memcpy(
                    probe->payload_names[index], entry->name,
                    entry->name_size);
                probe->payload_names[index][entry->name_size] = '\0';
            }
            if (entry->value.kind == SCXML_CONTENT_SCALAR) {
                probe->payload_values[index] = entry->value.scalar;
                if (entry->value.scalar.kind == SCXML_PAYLOAD_VALUE_STRING &&
                    entry->value.scalar.data.string.size <
                        sizeof(probe->payload_string)) {
                    memcpy(
                        probe->payload_string,
                        entry->value.scalar.data.string.data,
                        entry->value.scalar.data.string.size);
                    probe->payload_string[
                        entry->value.scalar.data.string.size] = '\0';
                    probe->payload_values[index].data.string.data =
                        probe->payload_string;
                }
            } else if (entry->value.kind == SCXML_CONTENT_CMETA) {
                probe->payload_schema = entry->value.schema;
                probe->payload_object = entry->value.object;
            }
        }
    }
    probe->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = send_cancel_commit,
        .discard = send_cancel_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status cmeta_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    send_cancel_probe *probe = (send_cancel_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        request->send_id == NULL || request->send_id_size == 0u ||
        request->send_id_size > SCXML_EVENT_METADATA_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->cancel_id, request->send_id, request->send_id_size);
    probe->cancel_id[request->send_id_size] = '\0';
    probe->cancel_id_size = request->send_id_size;
    probe->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = send_cancel_commit,
        .discard = send_cancel_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static void send_cancel_close(void *user) {
    ++((send_cancel_probe *)user)->close_count;
}

static bool send_cancel_quiescent(void *user) {
    (void)user;
    return true;
}

static const scxml_event_io_adapter send_cancel_adapter = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_PAYLOAD |
        SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL,
    .prepare_send = cmeta_prepare_send,
    .prepare_cancel = cmeta_prepare_cancel,
    .close = send_cancel_close,
    .is_quiescent = send_cancel_quiescent};

static void provider_ticket_commit(void *user) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (probe == NULL || !probe->live) return;
    probe->live = false;
    if (probe->published_dialog_id != NULL &&
        probe->published_dialog_id->size == probe->expected_dialog_id_size &&
        memcmp(
            probe->published_dialog_id->data,
            probe->expected_dialog_id,
            probe->expected_dialog_id_size) == 0) {
        probe->dialog_id_visible_at_commit = true;
    }
    ++probe->commit_count;
}

static void provider_ticket_discard(void *user) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (probe == NULL || !probe->live) return;
    probe->live = false;
    ++probe->discard_count;
}

static scxml_adapter_status unused_prepare_accept(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "accept is not used by this test";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status provider_prepare_conference(
    void *user, const ccxml_create_conference_request *request,
    ccxml_string_view *out_conference_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    (void)request;
    if (out_error != NULL) *out_error = NULL;
    probe->live = true;
    *out_conference_id = (ccxml_string_view){"conf-e2e", 8u};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status provider_prepare_destroy_conference(
    void *user, const ccxml_destroy_conference_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    probe->destroyed_conference_id_size = request->conference_id_size;
    memcpy(
        probe->destroyed_conference_id, request->conference_id,
        request->conference_id_size);
    probe->destroyed_conference_id[request->conference_id_size] = '\0';
    probe->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status provider_prepare_dialog_start(
    void *user, const ccxml_dialog_start_request *request,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    memcpy(probe->dialog_source, request->source, request->source_size);
    probe->dialog_source[request->source_size] = '\0';
    memcpy(
        probe->dialog_connection_id, request->connection_id,
        request->connection_id_size);
    probe->dialog_connection_id[request->connection_id_size] = '\0';
    memcpy(
        probe->dialog_media_type, request->media_type,
        request->media_type_size);
    probe->dialog_media_type[request->media_type_size] = '\0';
    probe->live = true;
    probe->expected_dialog_id = "dialog-e2e";
    probe->expected_dialog_id_size = 10u;
    *out_dialog_id = (ccxml_string_view){"dialog-e2e", 10u};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status provider_prepare_dialog_prepare(
    void *user, const ccxml_dialog_prepare_request *request,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    memcpy(probe->dialog_source, request->source, request->source_size);
    probe->dialog_source[request->source_size] = '\0';
    memcpy(
        probe->dialog_media_type, request->media_type,
        request->media_type_size);
    probe->dialog_media_type[request->media_type_size] = '\0';
    probe->live = true;
    probe->expected_dialog_id = "prepared-e2e";
    probe->expected_dialog_id_size = 12u;
    *out_dialog_id = (ccxml_string_view){"prepared-e2e", 12u};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status provider_prepare_dialog_terminate(
    void *user, const ccxml_dialog_terminate_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    memcpy(
        probe->terminated_dialog_id, request->dialog_id,
        request->dialog_id_size);
    probe->terminated_dialog_id[request->dialog_id_size] = '\0';
    probe->terminate_immediate = request->immediate;
    probe->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status provider_prepare_prepared_dialog_start(
    void *user, const ccxml_prepared_dialog_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    memcpy(
        probe->started_prepared_dialog_id, request->dialog_id,
        request->dialog_id_size);
    probe->started_prepared_dialog_id[request->dialog_id_size] = '\0';
    memcpy(
        probe->dialog_connection_id, request->connection_id,
        request->connection_id_size);
    probe->dialog_connection_id[request->connection_id_size] = '\0';
    probe->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static void provider_close(void *user) {
    ++((conference_provider_probe *)user)->close_count;
}

static bool provider_quiescent(void *user) {
    (void)user;
    return true;
}

static const ccxml_telephony_adapter_v1 conference_provider = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = unused_prepare_accept,
    .close = provider_close,
    .is_quiescent = provider_quiescent,
    .prepare_create_conference = provider_prepare_conference,
    .prepare_destroy_conference =
        provider_prepare_destroy_conference,
    .prepare_dialog_start = provider_prepare_dialog_start,
    .prepare_dialog_terminate =
        provider_prepare_dialog_terminate,
    .prepare_dialog_prepare =
        provider_prepare_dialog_prepare,
    .prepare_prepared_dialog_start =
        provider_prepare_prepared_dialog_start};

static ccxml_status initialize(
    ccxml_cmeta_datamodel *datamodel, test_state *state,
    size_t max_string_bytes) {
    const ccxml_cmeta_datamodel_config_v1 config = {
        .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
        .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
        .root = &state_desc,
        .state = state,
        .max_path_depth = 4u,
        .max_string_bytes = max_string_bytes};
    return ccxml_cmeta_datamodel_init(datamodel, &config);
}

spec("CCXML CMeta datamodel") {
    it("evaluates a typed root string expression") {
        test_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        ccxml_string_expression expression = {0};
        ccxml_string_view value = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const ccxml_event event = {.name = "go", .name_size = 2u};
        const char *error = NULL;

        memcpy(state.mode.data, "tel:root", sizeof("tel:root") - 1u);
        state.mode.size = sizeof("tel:root") - 1u;
        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        check_not_null(adapter->compile_string_expression);
        check_not_null(adapter->evaluate_string_expression);
        check_not_null(adapter->destroy_string_expression);
        if (adapter->compile_string_expression == NULL ||
            adapter->evaluate_string_expression == NULL ||
            adapter->destroy_string_expression == NULL) {
            ccxml_cmeta_datamodel_destroy(&datamodel);
            return;
        }
        check_equal(
            adapter->compile_string_expression(
                &datamodel, "mode", 4u, &expression, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_not_null(expression.impl);
        check_equal(
            adapter->evaluate_string_expression(
                &datamodel, &expression, &event, &value, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(value.size, sizeof("tel:root") - 1u);
        check_equal(value.data, "tel:root");

        adapter->destroy_string_expression(&datamodel, &expression);
        check_null(expression.impl);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects a non-string typed expression") {
        test_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        ccxml_string_expression expression = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const char *error = NULL;

        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        check_not_null(adapter->compile_string_expression);
        if (adapter->compile_string_expression == NULL) {
            ccxml_cmeta_datamodel_destroy(&datamodel);
            return;
        }
        check_equal(
            adapter->compile_string_expression(
                &datamodel, "count", 5u, &expression, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(expression.impl);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects an unknown typed string location") {
        test_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        ccxml_string_expression expression = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const char *error = NULL;

        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        check_not_null(adapter->compile_string_expression);
        if (adapter->compile_string_expression == NULL) {
            ccxml_cmeta_datamodel_destroy(&datamodel);
            return;
        }
        check_equal(
            adapter->compile_string_expression(
                &datamodel, "missing", 7u, &expression, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(expression.impl);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("evaluates the admitted CCXML event name operand") {
        test_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        ccxml_string_expression expression = {0};
        ccxml_string_view value = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const ccxml_event event = {
            .name = "connection.alerting",
            .name_size = sizeof("connection.alerting") - 1u};
        const char *error = NULL;

        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        check_equal(
            adapter->compile_string_expression(
                &datamodel, "_event.name", 11u, &expression, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(
            adapter->evaluate_string_expression(
                &datamodel, &expression, &event, &value, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(value.data, "connection.alerting");
        adapter->destroy_string_expression(&datamodel, &expression);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects unavailable SCXML system strings during admission") {
        test_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        ccxml_string_expression expression = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const char *error = NULL;

        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        check_equal(
            adapter->compile_string_expression(
                &datamodel, "_sessionid", 10u, &expression, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(expression.impl);
        check_equal(
            adapter->compile_string_expression(
                &datamodel, "_event.type", 11u, &expression, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(expression.impl);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects foreach elements without a semantic data descriptor") {
        foreach_record_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        scxml_scope_schema scope = {0};
        scxml_foreach_program program = {0};
        const char *error = NULL;
        const ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(config),
            .root = &foreach_record_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        check_equal(ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_true(scxml_scope_schema_init(&scope, 1u));
        check_equal(
            ccxml_cmeta_compile_foreach_scope(
                &datamodel, "values", 6u, "item", 4u, NULL, 0u, 4u,
                &scope, &program, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(program.sequence.root);

        scxml_scope_schema_destroy(&scope);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("uses a registered structured foreach item in conditions") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='values' item='item'>"
            "<if cond='item.code == 22'><accept/></if>"
            "</foreach>"
            "</transition></eventprocessor></ccxml>";
        static const ccxml_foreach_record elements[] = {
            {.code = 11}, {.code = 22}, {.code = 33}};
        const cmeta_data_desc *semantic_data[] = {
            &foreach_record_alias_data};
        ccxml_limits limits = ccxml_default_limits();
        foreach_record_state state = {
            .values = VecOf(ccxml_foreach_record)};
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        foreach_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "go", .name_size = 2u,
            .connection_id = "conn-1", .connection_id_size = 6u};
        size_t index;
        size_t item_slot = SIZE_MAX;
        const cmeta_data_desc *item_data = NULL;
        const void *item_object = NULL;
        const ccxml_cmeta_datamodel_config_v1 datamodel_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &foreach_record_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY,
            .semantic_data = semantic_data,
            .semantic_data_count = sizeof(semantic_data) /
                sizeof(semantic_data[0])};

        limits.max_foreach_iterations = 4u;
        check_equal(vec_init(&state.values, 4u), STL_OK);
        for (index = 0u; index < sizeof(elements) / sizeof(elements[0]);
             ++index)
            check_equal(vec_push(&state.values, &elements[index]), STL_OK);
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), &limits, &diagnostic),
            CCXML_OK);
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &datamodel_config),
            CCXML_OK);
        semantic_data[0] = NULL;
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &foreach_provider_adapter,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.prepare_count, (size_t)1u);
        check_equal(provider.commit_count, (size_t)1u);
        check_equal(provider.discard_count, (size_t)0u);
        {
            const ccxml_session_impl *impl =
                (const ccxml_session_impl *)session.impl;
            check_not_null(scxml_scope_find(
                &impl->foreach_scope, "item", 4u, &item_slot));
            check_true(scxml_scope_view_read(
                &impl->foreach_scope_committed, item_slot,
                &item_data, &item_object));
            check_true(cmeta_type_equal(
                item_data->storage_type,
                &cmeta_type_ccxml_foreach_record));
            check_true(item_data == &foreach_record_alias_data);
            check_equal(
                ((const ccxml_foreach_record *)item_object)->code, 33);
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_staged, item_slot,
                &item_data, &item_object));
        }

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        check_equal(provider.close_count, (size_t)1u);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
        vec_destroy(&state.values);
    }

    it("evaluates createcall destinations from the staged foreach item") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='values' item='item'>"
            "<createcall dest='item.destination'/>"
            "</foreach>"
            "</transition></eventprocessor></ccxml>";
        static const ccxml_foreach_record elements[] = {
            {.destination = {.size = 7u, .data = "tel:111"}, .code = 11},
            {.destination = {.size = 7u, .data = "tel:222"}, .code = 22},
            {.destination = {.size = 7u, .data = "tel:333"}, .code = 33}};
        const cmeta_data_desc *semantic_data[] = {
            &foreach_record_alias_data};
        ccxml_limits limits = ccxml_default_limits();
        foreach_record_state state = {
            .values = VecOf(ccxml_foreach_record)};
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        foreach_provider_probe provider = {.reject_on_prepare = 2u};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "go", .name_size = 2u,
            .connection_id = "conn-1", .connection_id_size = 6u};
        size_t index;
        size_t item_slot = SIZE_MAX;
        const cmeta_data_desc *item_data = NULL;
        const void *item_object = NULL;
        const ccxml_cmeta_datamodel_config_v1 datamodel_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &foreach_record_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY,
            .semantic_data = semantic_data,
            .semantic_data_count = sizeof(semantic_data) /
                sizeof(semantic_data[0])};

        limits.max_foreach_iterations = 4u;
        limits.max_foreach_storage_bytes = 4096u;
        check_equal(vec_init(&state.values, 4u), STL_OK);
        for (index = 0u; index < sizeof(elements) / sizeof(elements[0]);
             ++index)
            check_equal(vec_push(&state.values, &elements[index]), STL_OK);
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), &limits, &diagnostic),
            CCXML_OK);
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &datamodel_config),
            CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &foreach_provider_adapter,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);

        check_equal(
            ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
        check_equal(provider.prepare_count, (size_t)2u);
        check_equal(provider.destinations[0], "tel:111");
        check_equal(provider.destinations[1], "tel:222");
        check_equal(provider.commit_count, (size_t)0u);
        check_equal(provider.discard_count, (size_t)1u);
        {
            const ccxml_session_impl *impl =
                (const ccxml_session_impl *)session.impl;
            check_not_null(scxml_scope_find(
                &impl->foreach_scope, "item", 4u, &item_slot));
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_committed, item_slot,
                &item_data, &item_object));
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_staged, item_slot,
                &item_data, &item_object));
        }

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
        vec_destroy(&state.values);
    }

    it("evaluates redirect destinations from the staged foreach item") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='values' item='item'>"
            "<redirect dest='item.destination'/>"
            "</foreach>"
            "</transition></eventprocessor></ccxml>";
        static const ccxml_foreach_record elements[] = {
            {.destination = {.size = 7u, .data = "tel:111"}, .code = 11},
            {.destination = {.size = 7u, .data = "tel:222"}, .code = 22}};
        const cmeta_data_desc *semantic_data[] = {
            &foreach_record_alias_data};
        ccxml_limits limits = ccxml_default_limits();
        foreach_record_state state = {
            .values = VecOf(ccxml_foreach_record)};
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        foreach_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "go", .name_size = 2u,
            .connection_id = "conn-1", .connection_id_size = 6u};
        size_t index;
        const ccxml_cmeta_datamodel_config_v1 datamodel_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &foreach_record_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY,
            .semantic_data = semantic_data,
            .semantic_data_count = sizeof(semantic_data) /
                sizeof(semantic_data[0])};

        limits.max_foreach_iterations = 4u;
        limits.max_foreach_storage_bytes = 4096u;
        check_equal(vec_init(&state.values, 4u), STL_OK);
        for (index = 0u; index < sizeof(elements) / sizeof(elements[0]);
             ++index)
            check_equal(vec_push(&state.values, &elements[index]), STL_OK);
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), &limits, &diagnostic),
            CCXML_OK);
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &datamodel_config),
            CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &foreach_provider_adapter,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.prepare_count, (size_t)2u);
        check_equal(provider.destinations[0], "tel:111");
        check_equal(provider.destinations[1], "tel:222");
        check_equal(provider.commit_count, (size_t)2u);
        check_equal(provider.discard_count, (size_t)0u);
        check_equal(
            ((const ccxml_foreach_record *)state.values.data)[0]
                .destination.data,
            "tel:111");
        check_equal(
            ((const ccxml_foreach_record *)state.values.data)[1]
                .destination.data,
            "tel:222");

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
        vec_destroy(&state.values);
    }

    it("prefers a registered descriptor to the root fallback") {
        foreach_state state = {.values = VecOf(int)};
        ccxml_cmeta_datamodel datamodel = {0};
        scxml_scope_schema scope = {0};
        scxml_foreach_program program = {0};
        const cmeta_data_desc *semantic_data[] = {
            &foreach_int_override_data};
        const scxml_scope_slot *item_slot;
        const char *error = NULL;
        const ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(config),
            .root = &foreach_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY,
            .semantic_data = semantic_data,
            .semantic_data_count = sizeof(semantic_data) /
                sizeof(semantic_data[0])};

        check_equal(ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_true(scxml_scope_schema_init(&scope, 1u));
        check_equal(
            ccxml_cmeta_compile_foreach_scope(
                &datamodel, "values", 6u, "item", 4u, NULL, 0u, 4u,
                &scope, &program, &error),
            SCXML_ADAPTER_ACCEPTED);
        item_slot = scxml_scope_find(&scope, "item", 4u, NULL);
        check_not_null(item_slot);
        check_true(item_slot->value == &foreach_int_override_data);

        scxml_scope_schema_destroy(&scope);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects managed foreach elements without bounded copy storage") {
        foreach_managed_state state = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        scxml_scope_schema scope = {0};
        scxml_foreach_program program = {0};
        const char *error = NULL;
        const ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(config),
            .root = &foreach_managed_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        check_equal(ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_true(scxml_scope_schema_init(&scope, 1u));
        check_equal(
            ccxml_cmeta_compile_foreach_scope(
                &datamodel, "values", 6u, "item", 4u, NULL, 0u, 4u,
                &scope, &program, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(program.sequence.root);

        scxml_scope_schema_destroy(&scope);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects a foreach item that would alias application state") {
        foreach_state state = {.values = VecOf(int)};
        ccxml_cmeta_datamodel datamodel = {0};
        scxml_scope_schema scope = {0};
        scxml_foreach_program program = {0};
        const char *error = NULL;
        const ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(config),
            .root = &foreach_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        check_equal(ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_true(scxml_scope_schema_init(&scope, 1u));
        check_equal(
            ccxml_cmeta_compile_foreach_scope(
                &datamodel, "values", 6u, "existing", 8u, NULL, 0u, 4u,
                &scope, &program, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(program.sequence.root);

        scxml_scope_schema_destroy(&scope);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects a foreach index that would alias application state") {
        foreach_state state = {.values = VecOf(int), .root_index = 91u};
        ccxml_cmeta_datamodel datamodel = {0};
        scxml_scope_schema scope = {0};
        scxml_foreach_program program = {0};
        const char *error = NULL;
        const ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(config),
            .root = &foreach_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        check_equal(ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_true(scxml_scope_schema_init(&scope, 2u));
        check_equal(
            ccxml_cmeta_compile_foreach_scope(
                &datamodel, "values", 6u, "item", 4u,
                "root_index", 10u, 4u, &scope, &program, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(program.sequence.root);
        check_equal(state.root_index, (size_t)91u);

        scxml_scope_schema_destroy(&scope);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("uses the staged typed item to select foreach actions") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='values' item='item' index='index'><accept/>"
            "<if cond='item == 22 &amp;&amp; index == 1'><accept/></if>"
            "</foreach>"
            "</transition></eventprocessor></ccxml>";
        static const int elements[] = {11, 22, 33};
        ccxml_limits limits = ccxml_default_limits();
        foreach_state state = {.values = VecOf(int)};
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        foreach_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        ccxml_event event = {
            .name = "go", .name_size = 2u,
            .connection_id = "conn-1", .connection_id_size = 6u};
        size_t index;
        size_t item_slot = SIZE_MAX;
        size_t index_slot = SIZE_MAX;
        const cmeta_data_desc *item_data = NULL;
        const void *item_object = NULL;
        const ccxml_cmeta_datamodel_config_v1 datamodel_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &foreach_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        limits.max_foreach_iterations = 4u;
        limits.max_foreach_storage_bytes = 94u;
        check_equal(vec_init(&state.values, 5u), STL_OK);
        for (index = 0u; index < sizeof(elements) / sizeof(elements[0]);
             ++index)
            check_equal(vec_push(&state.values, &elements[index]), STL_OK);
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), &limits, &diagnostic),
            CCXML_OK);
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &datamodel_config),
            CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &foreach_provider_adapter,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.prepare_count, (size_t)4u);
        check_equal(provider.commit_count, (size_t)4u);
        check_equal(provider.discard_count, (size_t)0u);
        {
            const ccxml_session_impl *impl =
                (const ccxml_session_impl *)session.impl;
            check_not_null(scxml_scope_find(
                &impl->foreach_scope, "item", 4u, &item_slot));
            check_true(scxml_scope_view_read(
                &impl->foreach_scope_committed, item_slot,
                &item_data, &item_object));
            check_true(cmeta_type_equal(
                item_data->storage_type, &cmeta_type_int));
            check_equal(*(const int *)item_object, 33);
            check_not_null(scxml_scope_find(
                &impl->foreach_scope, "index", 5u, &index_slot));
            check_true(scxml_scope_view_read(
                &impl->foreach_scope_committed, index_slot,
                &item_data, &item_object));
            check_true(cmeta_type_equal(
                item_data->storage_type, &cmeta_type_size));
            check_equal(*(const size_t *)item_object, (size_t)2u);
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_staged, item_slot,
                &item_data, &item_object));
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_staged, index_slot,
                &item_data, &item_object));
        }

        {
            const int fourth = 44;
            const int fifth = 55;
            const ccxml_session_impl *impl =
                (const ccxml_session_impl *)session.impl;
            check_equal(vec_push(&state.values, &fourth), STL_OK);
            check_equal(vec_push(&state.values, &fifth), STL_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_LIMIT_EXCEEDED);
            check_equal(provider.prepare_count, (size_t)4u);
            check_equal(provider.commit_count, (size_t)4u);
            check_true(scxml_scope_view_read(
                &impl->foreach_scope_committed, item_slot,
                &item_data, &item_object));
            check_equal(*(const int *)item_object, 33);
        }

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        check_equal(provider.close_count, (size_t)1u);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
        vec_destroy(&state.values);
    }

    it("rolls back typed foreach effects after a later iteration fails") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='values' item='item'><accept/></foreach>"
            "</transition></eventprocessor></ccxml>";
        static const int elements[] = {1, 2, 3};
        ccxml_limits limits = ccxml_default_limits();
        foreach_state state = {.values = VecOf(int)};
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        foreach_provider_probe provider = {.reject_on_prepare = 2u};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        ccxml_event event = {
            .name = "go", .name_size = 2u,
            .connection_id = "conn-1", .connection_id_size = 6u};
        size_t index;
        const ccxml_cmeta_datamodel_config_v1 datamodel_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &foreach_state_desc,
            .state = &state,
            .max_path_depth = 3u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        limits.max_foreach_iterations = 4u;
        check_equal(vec_init(&state.values, 4u), STL_OK);
        for (index = 0u; index < sizeof(elements) / sizeof(elements[0]);
             ++index)
            check_equal(vec_push(&state.values, &elements[index]), STL_OK);
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), &limits, &diagnostic),
            CCXML_OK);
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &datamodel_config),
            CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &foreach_provider_adapter,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);

        check_equal(
            ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
        check_equal(provider.prepare_count, (size_t)2u);
        check_equal(provider.commit_count, (size_t)0u);
        check_equal(provider.discard_count, (size_t)1u);
        {
            const ccxml_session_impl *impl =
                (const ccxml_session_impl *)session.impl;
            size_t item_slot = SIZE_MAX;
            const cmeta_data_desc *item_data = NULL;
            const void *item_object = NULL;
            check_not_null(scxml_scope_find(
                &impl->foreach_scope, "item", 4u, &item_slot));
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_committed, item_slot,
                &item_data, &item_object));
            check_false(scxml_scope_view_read(
                &impl->foreach_scope_staged, item_slot,
                &item_data, &item_object));
        }

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
        vec_destroy(&state.values);
    }

    it("rejects payload scalars whose storage is narrower than reflection") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='send.now'>"
            "<send target=\"'session:callee'\" name=\"'call.notice'\" "
            "namelist='bad'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        malformed_payload_state state = {0};
        conference_provider_probe provider = {0};
        send_cancel_probe event_io = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_cmeta_datamodel_config_v1 datamodel_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &malformed_state_desc,
            .state = &state,
            .max_path_depth = 2u,
            .max_string_bytes = TEST_TEXT_CAPACITY};

        check_true(cmeta_data_desc_valid(&malformed_integer_desc));
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &datamodel_config),
            CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel,
            .event_io = &send_cancel_adapter,
            .event_io_user = &event_io};
        check_equal(
            ccxml_session_init(&session, &session_config),
            CCXML_INVALID_ARGUMENT);
        check_null(session.impl);

        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("sends scalar and object namelist values through CMeta") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='send.now'>"
            "<send target=\"'session:callee'\" name=\"'call.notice'\" "
            "sendid='dialog.id' "
            "namelist='conference.id count conference'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        send_cancel_probe event_io = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "send.now",
            .name_size = sizeof("send.now") - 1u};

        memcpy(state.conference.id.data, "conf-42", 7u);
        state.conference.id.data[7] = '\0';
        state.conference.id.size = 7u;
        state.count = -9;
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel,
            .event_io = &send_cancel_adapter,
            .event_io_user = &event_io};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        if (session.impl != NULL) {
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(event_io.payload_count, (size_t)3u);
            check_equal(event_io.payload_names[0], "conference.id");
            check_equal(event_io.payload_names[1], "count");
            check_equal(event_io.payload_names[2], "conference");
            check_equal(
                event_io.payload_values[0].kind,
                SCXML_PAYLOAD_VALUE_STRING);
            check_equal(event_io.payload_string, "conf-42");
            check_equal(
                event_io.payload_values[1].kind,
                SCXML_PAYLOAD_VALUE_SINT);
            check_equal(event_io.payload_values[1].data.sint, INT64_C(-9));
            check_true(event_io.payload_schema == &conference_desc);
            check_true(event_io.payload_object == &state.conference);
        }

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("routes repeated events through statevariable assignments") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'idle'\"/>"
            "<eventprocessor statevariable='mode'>"
            "<transition state='waiting idle' event='advance'>"
            "<assign name='mode' expr=\"'active'\"/>"
            "</transition>"
            "<transition state='active' event='advance'><exit/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "advance",
            .name_size = sizeof("advance") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(state.mode.data, "idle");

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(state.mode.data, "active");
        check_false(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("reevaluates CMeta transition conditions after assignment") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'idle'\"/>"
            "<eventprocessor>"
            "<transition event='advance' "
            "cond='mode == &quot;active&quot;'><exit/></transition>"
            "<transition event='advance' "
            "cond='mode == &quot;idle&quot; &amp;&amp; "
            "_event.name == &quot;advance&quot;'>"
            "<assign name='mode' expr=\"'active'\"/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "advance",
            .name_size = sizeof("advance") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(state.mode.data, "idle");

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(state.mode.data, "active");
        check_false(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("rejects an invalid condition before committing root variables") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'idle'\"/>"
            "<eventprocessor><transition event='advance' cond='mode =='>"
            "<exit/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};

        check_equal(
            ccxml_session_init(&session, &session_config),
            CCXML_ADAPTER_ERROR);
        check_null(session.impl);
        check_equal(state.mode.size, (size_t)0);

        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("rejects SCXML-only system operands in CCXML conditions") {
        static const char *const unsupported_conditions[] = {
            "_event.type == \"external\"",
            "_event.data == \"payload\"",
            "_name == \"machine\"",
            "_sessionid != \"\"",
            "_ioprocessors.scxml.location != \"\"",
            "isBound(_event.name)",
            "In(\"active\")"};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        size_t index;

        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        for (index = 0u;
             index < sizeof(unsupported_conditions) /
                         sizeof(unsupported_conditions[0]);
             ++index) {
            const char *source = unsupported_conditions[index];
            ccxml_condition condition = {0};
            const char *error = NULL;
            check_equal(
                adapter->compile_condition(
                    &datamodel, source, strlen(source),
                    &condition, &error),
                SCXML_ADAPTER_ERROR_EXECUTION);
            check_null(condition.impl);
            check_not_null(error);
        }

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rolls back a state assignment when a later action fails") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'waiting'\"/>"
            "<eventprocessor statevariable='mode'>"
            "<transition state='waiting' event='connection.alerting'>"
            "<assign name='mode' expr=\"'active'\"/><accept/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "connection.alerting",
            .name_size = sizeof("connection.alerting") - 1u,
            .connection_id = "call-e2e",
            .connection_id_size = sizeof("call-e2e") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(state.mode.data, "waiting");

        check_equal(
            ccxml_session_dispatch(&session, &event),
            CCXML_ADAPTER_ERROR);
        check_equal(state.mode.data, "waiting");
        check_false(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("prepares, starts, and terminates a dialog through nested CMeta") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='ccxml.loaded'>"
            "<dialogprepare dialogid='dialog.id' src=\"'menu.vxml'\"/>"
            "</transition><transition event='connection.connected'>"
            "<dialogstart prepareddialogid='dialog.id' "
            "connectionid='event$.connectionid'/>"
            "</transition><transition event='dialog.stop'>"
            "<dialogterminate dialogid='dialog.id'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {
            .published_dialog_id = &state.dialog.id};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "ccxml.loaded",
            .name_size = sizeof("ccxml.loaded") - 1u};
        const ccxml_event connected_event = {
            .name = "connection.connected",
            .name_size = sizeof("connection.connected") - 1u,
            .connection_id = "call-e2e",
            .connection_id_size = sizeof("call-e2e") - 1u};
        const ccxml_event stop_event = {
            .name = "dialog.stop",
            .name_size = sizeof("dialog.stop") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.dialog_source, "menu.vxml");
        check_equal(provider.dialog_connection_id, "");
        check_equal(
            provider.dialog_media_type,
            "application/voicexml+xml");
        check_true(provider.dialog_id_visible_at_commit);
        check_equal(state.dialog.id.data, "prepared-e2e");
        check_equal(state.dialog.id.size, (size_t)12);
        check_equal(
            ccxml_session_dispatch(&session, &connected_event), CCXML_OK);
        check_equal(provider.started_prepared_dialog_id, "prepared-e2e");
        check_equal(provider.dialog_connection_id, "call-e2e");
        check_equal(
            ccxml_session_dispatch(&session, &stop_event), CCXML_OK);
        check_equal(provider.terminated_dialog_id, "prepared-e2e");
        check_false(provider.terminate_immediate);
        check_equal(provider.commit_count, (size_t)3);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("starts and normally terminates a dialog through nested CMeta") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='connection.alerting'>"
            "<dialogstart dialogid='dialog.id' "
            "src=\"'menu.vxml'\" "
            "connectionid='event$.connectionid'/>"
            "</transition><transition event='dialog.stop'>"
            "<dialogterminate dialogid='dialog.id'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {
            .published_dialog_id = &state.dialog.id};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "connection.alerting",
            .name_size = sizeof("connection.alerting") - 1u,
            .connection_id = "call-e2e",
            .connection_id_size = sizeof("call-e2e") - 1u};
        const ccxml_event stop_event = {
            .name = "dialog.stop",
            .name_size = sizeof("dialog.stop") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.dialog_source, "menu.vxml");
        check_equal(provider.dialog_connection_id, "call-e2e");
        check_equal(
            provider.dialog_media_type,
            "application/voicexml+xml");
        check_true(provider.dialog_id_visible_at_commit);
        check_equal(state.dialog.id.data, "dialog-e2e");
        check_equal(state.dialog.id.size, (size_t)10);
        check_equal(
            ccxml_session_dispatch(&session, &stop_event), CCXML_OK);
        check_equal(provider.terminated_dialog_id, "dialog-e2e");
        check_false(provider.terminate_immediate);
        check_equal(provider.commit_count, (size_t)2);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("writes a provider-generated ID through a CCXML session") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='ccxml.loaded'>"
            "<createconference conferenceid='conference.id'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "ccxml.loaded",
            .name_size = sizeof("ccxml.loaded") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.commit_count, (size_t)1);
        check_equal(provider.discard_count, (size_t)0);
        check_equal(state.conference.id.data, "conf-e2e");

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("writes and later cancels a send ID through nested CMeta") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='send.now'>"
            "<send target=\"'session:callee'\" name=\"'call.notice'\" "
            "delay=\"'10ms'\" sendid='conference.id'/>"
            "</transition><transition event='cancel.now'>"
            "<cancel sendid='conference.id'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        send_cancel_probe event_io = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event send_event = {
            .name = "send.now",
            .name_size = sizeof("send.now") - 1u};
        const ccxml_event cancel_event = {
            .name = "cancel.now",
            .name_size = sizeof("cancel.now") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(
            initialize(&datamodel, &state, TEST_TEXT_CAPACITY), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel,
            .event_io = &send_cancel_adapter,
            .event_io_user = &event_io};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(
            ccxml_session_dispatch(&session, &send_event), CCXML_OK);
        check_true(state.conference.id.size > sizeof("send.") - 1u);
        check_equal(state.conference.id.size, event_io.send_id_size);
        check_equal(state.conference.id.data, event_io.send_id);
        check_equal(
            ccxml_session_dispatch(&session, &cancel_event), CCXML_OK);
        check_equal(event_io.cancel_id_size, event_io.send_id_size);
        check_equal(event_io.cancel_id, event_io.send_id);
        check_equal(event_io.commit_count, (size_t)2u);
        check_equal(event_io.discard_count, (size_t)0u);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        check_equal(event_io.close_count, (size_t)1u);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("stages and commits a nested owned-string write") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const char *error = NULL;

        memcpy(state.conference.id.data, "old", 3u);
        state.conference.id.data[3] = '\0';
        state.conference.id.size = 3u;
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "conference.id", 13u, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(
            adapter->prepare_assign_string(
                &datamodel, "conference.id", 13u,
                "conf-42", 7u, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(state.conference.id.data, "old");
        ticket.commit(ticket.user);
        check_equal(state.conference.id.data, "conf-42");
        check_equal(state.conference.id.size, (size_t)7);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("reads a nested conference ID through a CCXML session") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='ccxml.loaded'>"
            "<destroyconference conferenceid='conference.id'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "ccxml.loaded",
            .name_size = sizeof("ccxml.loaded") - 1u};

        memcpy(state.conference.id.data, "conf-live", 9u);
        state.conference.id.data[9] = '\0';
        state.conference.id.size = 9u;
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        if (session.impl != NULL) {
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.destroyed_conference_id, "conf-live");
            check_equal(provider.commit_count, (size_t)1);
            check_equal(state.conference.id.data, "conf-live");
            check_equal(state.conference.id.size, (size_t)9);
        }

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("returns a bounded borrowed nested string view") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        ccxml_string_view value = {0};

        memcpy(state.conference.id.data, "conf-read", 9u);
        state.conference.id.data[9] = '\0';
        state.conference.id.size = 9u;
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_not_null(adapter->validate_readable_string_location);
        check_not_null(adapter->read_string);
        if (adapter->validate_readable_string_location != NULL &&
            adapter->read_string != NULL) {
            check_equal(
                adapter->validate_readable_string_location(
                    &datamodel, "conference.id", 13u, NULL),
                SCXML_ADAPTER_ACCEPTED);
            check_equal(
                adapter->read_string(
                    &datamodel, "conference.id", 13u, &value, NULL),
                SCXML_ADAPTER_ACCEPTED);
            check_equal(value.size, (size_t)9);
            check_equal((const char *)value.data, "conf-read");
            check_equal(state.conference.id.data, "conf-read");
        }

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("reads a borrowed string that is not a writable location") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        ccxml_string_view value = {0};

        memcpy(state.read_only.data, "conf-view", 9u);
        state.read_only.data[9] = '\0';
        state.read_only.size = 9u;
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "read_only", 9u, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(
            adapter->validate_readable_string_location(
                &datamodel, "read_only", 9u, NULL),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(
            adapter->read_string(
                &datamodel, "read_only", 9u, &value, NULL),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(value.size, (size_t)9);
        check_equal((const char *)value.data, "conf-view");

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects unreadable paths and non-string fields") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_not_null(adapter->validate_readable_string_location);
        if (adapter->validate_readable_string_location != NULL) {
            check_equal(
                adapter->validate_readable_string_location(
                    &datamodel, "conference.missing", 18u, NULL),
                SCXML_ADAPTER_ERROR_EXECUTION);
            check_equal(
                adapter->validate_readable_string_location(
                    &datamodel, "count", 5u, NULL),
                SCXML_ADAPTER_ERROR_EXECUTION);
        }

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects a string view above the configured read bound") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        ccxml_string_view value = {(const char *)1, 99u};

        memcpy(state.conference.id.data, "conf-wide", 9u);
        state.conference.id.data[9] = '\0';
        state.conference.id.size = 9u;
        check_equal(initialize(&datamodel, &state, 4u), CCXML_OK);
        check_not_null(adapter->read_string);
        if (adapter->read_string != NULL) {
            check_equal(
                adapter->read_string(
                    &datamodel, "conference.id", 13u, &value, NULL),
                SCXML_ADAPTER_ERROR_EXECUTION);
            check_null(value.data);
            check_equal(value.size, (size_t)0);
        }
        check_equal(state.conference.id.data, "conf-wide");

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects empty and NUL-containing conference ID views") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        ccxml_string_view value = {(const char *)1, 99u};

        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->read_string(
                &datamodel, "conference.id", 13u, &value, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(value.data);
        check_equal(value.size, (size_t)0);

        memcpy(state.conference.id.data, "ab\0cd", 5u);
        state.conference.id.size = 5u;
        value = (ccxml_string_view){(const char *)1, 99u};
        check_equal(
            adapter->read_string(
                &datamodel, "conference.id", 13u, &value, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(value.data);
        check_equal(value.size, (size_t)0);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("discard leaves the live value unchanged") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        memcpy(state.conference.id.data, "old", 3u);
        state.conference.id.data[3] = '\0';
        state.conference.id.size = 3u;
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->prepare_assign_string(
                &datamodel, "conference.id", 13u,
                "conf-43", 7u, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.discard(ticket.user);
        check_equal(state.conference.id.data, "old");
        check_equal(state.conference.id.size, (size_t)3);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects unresolved and non-string locations") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "conference.missing", 18u, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "count", 5u, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("enforces the configured write bound during prepare") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        check_equal(initialize(&datamodel, &state, 4u), CCXML_OK);
        check_equal(
            adapter->prepare_assign_string(
                &datamodel, "conference.id", 13u,
                "conf-44", 7u, &ticket, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_equal(state.conference.id.size, (size_t)0);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects invalid owner and config contracts") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &state_desc,
            .state = &state,
            .max_path_depth = 4u,
            .max_string_bytes = 16u};

        config.max_path_depth = 0u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.max_path_depth = 4u;
        config.abi_version = 0u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        check_null(datamodel.impl);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("size-gates the optional CMeta config tail") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = offsetof(
                ccxml_cmeta_datamodel_config_v1, semantic_data),
            .root = &state_desc,
            .state = &state,
            .max_path_depth = 4u,
            .max_string_bytes = 16u};
        struct {
            ccxml_cmeta_datamodel_config_v1 config;
            unsigned char future_tail;
        } future_config = {.config = config};

        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_not_null(datamodel.impl);
        ccxml_cmeta_datamodel_destroy(&datamodel);

        --config.struct_size;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.struct_size = offsetof(
            ccxml_cmeta_datamodel_config_v1, semantic_data) + 1u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        future_config.config.struct_size = sizeof(future_config);
        check_equal(
            ccxml_cmeta_datamodel_init(
                &datamodel, &future_config.config),
            CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("validates the optional semantic descriptor registry") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const cmeta_data_desc *valid[] = {&foreach_record_data};
        const cmeta_data_desc *duplicates[] = {
            &foreach_record_data, &foreach_record_alias_data};
        const cmeta_data_desc *invalid[] = {&cmeta_data_sequence};
        ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &state_desc,
            .state = &state,
            .max_path_depth = 4u,
            .max_string_bytes = 16u,
            .semantic_data = valid,
            .semantic_data_count = 0u};

        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.semantic_data = NULL;
        config.semantic_data_count = 1u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.semantic_data = invalid;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.semantic_data = valid;
        config.semantic_data_count = SIZE_MAX;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.semantic_data = duplicates;
        config.semantic_data_count = 2u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.semantic_data = valid;
        config.semantic_data_count = 1u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
        check_not_null(datamodel.impl);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }
}
