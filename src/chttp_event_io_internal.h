#ifndef SCXML_CHTTP_EVENT_IO_INTERNAL_H
#define SCXML_CHTTP_EVENT_IO_INTERNAL_H

#include <scxml/chttp_event_io.h>

typedef struct scxml_chttp_decoded_form {
    const char *event_name;
    size_t event_name_size;
    const scxml_chttp_form_entry_view *entries;
    size_t entry_count;
    size_t storage_size;
} scxml_chttp_decoded_form;

scxml_adapter_status scxml_chttp_encode_send_body(
    const scxml_send_request *request,
    void *out_body, size_t body_capacity, size_t *out_body_size,
    const char **out_content_type, size_t *out_content_type_size);

scxml_chttp_decode_status scxml_chttp_decode_form_body(
    const void *body, size_t body_size,
    char *storage, size_t storage_capacity,
    scxml_chttp_form_entry_view *entries, size_t entry_capacity,
    size_t max_event_name_bytes, size_t max_form_name_bytes,
    size_t max_form_value_bytes, scxml_chttp_decoded_form *out);

#endif
