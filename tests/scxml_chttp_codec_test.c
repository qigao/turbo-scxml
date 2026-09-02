#include "chttp_event_io_internal.h"

#include "tinytest.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static scxml_content_view scalar_string(const char *data) {
    scxml_content_view value = {
        .kind = SCXML_CONTENT_SCALAR,
        .scalar = {
            .kind = SCXML_PAYLOAD_VALUE_STRING,
            .data.string = {data, data != NULL ? strlen(data) : 0u}}};
    return value;
}

static scxml_chttp_decode_status decode(
    const char *body, char *storage, size_t storage_capacity,
    scxml_chttp_form_entry_view *entries, size_t entry_capacity,
    scxml_chttp_decoded_form *out) {
    return scxml_chttp_decode_form_body(
        body, strlen(body), storage, storage_capacity,
        entries, entry_capacity, 64u, 64u, 64u, out);
}

suite("SCXML CHTTP BasicHTTP codec") {
    it("encodes the event and named payload in stable form order") {
        static const char expected[] =
            "_scxmleventname=order+ready&customer=A%26B&qty=2";
        scxml_payload_entry entries[2] = {
            {"customer", 8u, {0}},
            {"qty", 3u, {
                .kind = SCXML_CONTENT_SCALAR,
                .scalar = {
                    .kind = SCXML_PAYLOAD_VALUE_SINT,
                    .data.sint = 2}}}};
        scxml_send_request request = {
            .event = "order ready",
            .event_size = sizeof("order ready") - 1u,
            .payload = {
                .kind = SCXML_PAYLOAD_NAMED,
                .entries = entries,
                .entry_count = 2u}};
        char body[sizeof(expected) - 1u];
        const char *content_type = NULL;
        size_t content_type_size = 0u;
        size_t body_size = 0u;

        entries[0].value = scalar_string("A&B");
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &body_size,
                        &content_type, &content_type_size),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(body_size, sizeof(expected) - 1u);
        check_equal(body, expected, sizeof(expected) - 1u);
        check_equal(content_type, "application/x-www-form-urlencoded",
                    sizeof("application/x-www-form-urlencoded") - 1u);
        check_equal(content_type_size,
                    sizeof("application/x-www-form-urlencoded") - 1u);
    }

    it("uses stable lexical forms for every supported scalar kind") {
        static const char expected[] =
            "_scxmleventname=test&b=true&i=-7&u=9&f=1.5&s=a+b";
        scxml_payload_entry entries[5] = {
            {"b", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_BOOL,
                                   .data.boolean = true}}},
            {"i", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_SINT,
                                   .data.sint = -7}}},
            {"u", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_UINT,
                                   .data.uint = UINT64_C(9)}}},
            {"f", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_FLOAT,
                                   .data.number = 1.5}}},
            {"s", 1u, {0}}};
        scxml_send_request request = {
            .event = "test", .event_size = 4u,
            .payload = {SCXML_PAYLOAD_NAMED, {0}, entries, 5u}};
        char body[sizeof(expected) - 1u];
        size_t size = 0u;
        const char *type = NULL;
        size_t type_size = 0u;
        entries[4].value = scalar_string("a b");

        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(size, sizeof(expected) - 1u);
        check_equal(body, expected, sizeof(expected) - 1u);
    }

    it("accepts one reserved Event-name param when send event is absent") {
        static const char expected[] = "_scxmleventname=test";
        scxml_payload_entry entry = {
            "_scxmleventname", sizeof("_scxmleventname") - 1u, {0}};
        scxml_send_request request = {
            .payload = {
                .kind = SCXML_PAYLOAD_NAMED,
                .entries = &entry,
                .entry_count = 1u}};
        char body[sizeof(expected) - 1u];
        size_t size = 0u;
        const char *type = NULL;
        size_t type_size = 0u;

        entry.value = scalar_string("test");
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(size, sizeof(expected) - 1u);
        check_equal(body, expected, sizeof(expected) - 1u);

        request.payload = (scxml_payload_view){0};
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ERROR_EXECUTION);
    }

    it("measures before writing and rejects invalid scalar strings") {
        scxml_payload_entry entry = {"value", 5u, {0}};
        scxml_send_request request = {
            .event = "test", .event_size = 4u,
            .payload = {SCXML_PAYLOAD_NAMED, {0}, &entry, 1u}};
        char body[64] = "unchanged";
        size_t required = 0u;
        const char *type = NULL;
        size_t type_size = 0u;

        entry.value = scalar_string("hello");
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, 4u, &required, &type, &type_size),
                    SCXML_ADAPTER_FULL);
        check_true(required > 4u);
        check_equal(body, "unchanged");
        entry.value.scalar.data.string.data = "\xC0\xAF";
        entry.value.scalar.data.string.size = 2u;
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &required,
                        &type, &type_size),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        entry.value.scalar.kind = SCXML_PAYLOAD_VALUE_FLOAT;
        entry.value.scalar.data.number = NAN;
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &required,
                        &type, &type_size),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        entry.name = "_scxmleventname";
        entry.name_size = sizeof("_scxmleventname") - 1u;
        entry.value = scalar_string("duplicate");
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &required,
                        &type, &type_size),
                    SCXML_ADAPTER_ERROR_EXECUTION);
    }

    it("copies text XML and scalar content while rejecting CMeta content") {
        static const char text[] = "this is some content";
        static const char xml[] = "<order id='7'/>";
        scxml_send_request request = {.event = "test", .event_size = 4u};
        char body[64] = {0};
        const char *type = NULL;
        size_t type_size = 0u;
        size_t size = 0u;

        request.payload.kind = SCXML_PAYLOAD_CONTENT;
        request.payload.content = (scxml_content_view){
            .kind = SCXML_CONTENT_TEXT_UTF8,
            .bytes = text, .byte_count = sizeof(text) - 1u};
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(body, text, sizeof(text) - 1u);
        check_equal(type, "text/plain; charset=utf-8",
                    sizeof("text/plain; charset=utf-8") - 1u);

        request.payload.content = (scxml_content_view){
            .kind = SCXML_CONTENT_XML_UTF8,
            .bytes = xml, .byte_count = sizeof(xml) - 1u};
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(body, xml, sizeof(xml) - 1u);
        check_equal(type, "application/xml; charset=utf-8",
                    sizeof("application/xml; charset=utf-8") - 1u);

        request.payload.content = (scxml_content_view){
            .kind = SCXML_CONTENT_SCALAR,
            .scalar = {.kind = SCXML_PAYLOAD_VALUE_BOOL,
                       .data.boolean = false}};
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(body, "false", 5u);

        request.payload.content = (scxml_content_view){
            .kind = SCXML_CONTENT_CMETA};
        check_equal(scxml_chttp_encode_send_body(
                        &request, body, sizeof(body), &size,
                        &type, &type_size),
                    SCXML_ADAPTER_ERROR_EXECUTION);
    }

    it("decodes one reserved Event name and retains duplicate fields") {
        char storage[64];
        scxml_chttp_form_entry_view entries[2];
        scxml_chttp_decoded_form form = {0};

        check_equal(decode(
                        "_scxmleventname=test&tag=one&tag=two",
                        storage, sizeof(storage), entries, 2u, &form),
                    SCXML_CHTTP_DECODE_OK);
        check_equal(form.event_name, "test", 4u);
        check_equal(form.entry_count, (size_t)2u);
        check_equal(form.entries[0].name, "tag", 3u);
        check_equal(form.entries[0].value, "one", 3u);
        check_equal(form.entries[1].name, "tag", 3u);
        check_equal(form.entries[1].value, "two", 3u);
    }

    it("decodes plus and percent forms and defaults the Event name") {
        char storage[64];
        scxml_chttp_form_entry_view entries[1];
        scxml_chttp_decoded_form form = {0};

        check_equal(decode(
                        "label=hello+world%21", storage, sizeof(storage),
                        entries, 1u, &form),
                    SCXML_CHTTP_DECODE_OK);
        check_equal(form.event_name, "HTTP.POST", 9u);
        check_equal(form.entries[0].value, "hello world!", 12u);
    }

    it("rejects malformed duplicate empty NUL and invalid UTF-8 forms") {
        static const char *const invalid[] = {
            "_scxmleventname=test&bad=%G0",
            "_scxmleventname=test&bad=%00",
            "_scxmleventname=test&bad=%C0%AF",
            "_scxmleventname=one&_scxmleventname=two",
            "_scxmleventname="};
        char storage[128];
        scxml_chttp_form_entry_view entries[4];
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            scxml_chttp_decoded_form form = {
                .event_name = "unchanged", .entry_count = 99u};
            check_equal(decode(
                            invalid[index], storage, sizeof(storage),
                            entries, 4u, &form),
                        SCXML_CHTTP_DECODE_BAD_REQUEST);
            check_null(form.event_name);
            check_equal(form.entry_count, (size_t)0u);
        }
    }

    it("enforces entry field and scratch capacities before publication") {
        static const char body[] =
            "one=1&two=22&_scxmleventname=test";
        char storage[32];
        scxml_chttp_form_entry_view entries[2];
        scxml_chttp_decoded_form form = {0};

        check_equal(scxml_chttp_decode_form_body(
                        body, sizeof(body) - 1u, storage, sizeof(storage),
                        entries, 1u, 64u, 64u, 64u, &form),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        check_equal(form.entry_count, (size_t)0u);
        check_equal(scxml_chttp_decode_form_body(
                        body, sizeof(body) - 1u, storage, 17u,
                        entries, 2u, 64u, 64u, 64u, &form),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        check_equal(scxml_chttp_decode_form_body(
                        body, sizeof(body) - 1u, storage, 18u,
                        entries, 2u, 2u, 64u, 64u, &form),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        check_equal(scxml_chttp_decode_form_body(
                        body, sizeof(body) - 1u, storage, 18u,
                        entries, 2u, 64u, 1u, 64u, &form),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        check_equal(scxml_chttp_decode_form_body(
                        body, sizeof(body) - 1u, storage, 18u,
                        entries, 2u, 64u, 64u, 1u, &form),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        check_equal(scxml_chttp_decode_form_body(
                        body, sizeof(body) - 1u, storage, 18u,
                        entries, 2u, 64u, 64u, 64u, &form),
                    SCXML_CHTTP_DECODE_OK);
        check_equal(form.entry_count, (size_t)2u);
    }
}
