#ifndef SCXML_CHTTP_EVENT_IO_INTERNAL_H
#define SCXML_CHTTP_EVENT_IO_INTERNAL_H

#include <scxml/chttp_event_io.h>

typedef struct scxml_chttp_codec_limits {
    size_t max_event_name_bytes;
    size_t max_form_entry_count;
    size_t max_form_name_bytes;
    size_t max_form_value_bytes;
    size_t max_encoded_body_bytes;
} scxml_chttp_codec_limits;

typedef struct scxml_chttp_encoded_body {
    const char *media_type;
    size_t media_type_size;
    size_t body_size;
} scxml_chttp_encoded_body;

typedef struct scxml_chttp_decoded_form {
    const char *event;
    size_t event_size;
    scxml_chttp_form_entry_view *entries;
    size_t entry_count;
} scxml_chttp_decoded_form;

scxml_adapter_status scxml_chttp_codec_encode(
    const scxml_send_request *request,
    const scxml_chttp_codec_limits *limits,
    char *body, size_t body_capacity,
    size_t *out_required_body_size,
    scxml_chttp_encoded_body *out);

scxml_chttp_decode_status scxml_chttp_codec_decode_form(
    const void *body, size_t body_size,
    const scxml_chttp_codec_limits *limits,
    scxml_chttp_form_entry_view *entry_storage,
    size_t entry_capacity,
    char *text_storage, size_t text_capacity,
    size_t *out_required_text_size,
    scxml_chttp_decoded_form *out);

#endif /* SCXML_CHTTP_EVENT_IO_INTERNAL_H */
