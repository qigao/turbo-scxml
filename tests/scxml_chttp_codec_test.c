#include <scxml/chttp_event_io.h>

#include "chttp_event_io_internal.h"

#include <tinytest.h>

#include <math.h>
#include <locale.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static scxml_chttp_codec_limits codec_limits(void) {
    const scxml_chttp_codec_limits limits = {
        64u, 8u, 32u, 64u, 512u};
    return limits;
}

static scxml_content_view scalar_string(const char *data, size_t size) {
    scxml_content_view content = {0};
    content.kind = SCXML_CONTENT_SCALAR;
    content.scalar.kind = SCXML_PAYLOAD_VALUE_STRING;
    content.scalar.data.string.data = data;
    content.scalar.data.string.size = size;
    return content;
}

spec("TurboSCXML pure BasicHTTP codec") {
    it("encodes the event and named payload in stable form order") {
        static const char expected[] =
            "_scxmleventname=order+ready&customer=A%26B&qty=2";
        const scxml_payload_entry entries[] = {
            {"customer", 8u, {
                .kind = SCXML_CONTENT_SCALAR,
                .scalar = {.kind = SCXML_PAYLOAD_VALUE_STRING,
                           .data.string = {"A&B", 3u}}}},
            {"qty", 3u, {
                .kind = SCXML_CONTENT_SCALAR,
                .scalar = {.kind = SCXML_PAYLOAD_VALUE_SINT,
                           .data.sint = 2}}}};
        const scxml_send_request request = {
            .event = "order ready", .event_size = 11u,
            .payload = {.kind = SCXML_PAYLOAD_NAMED,
                        .entries = entries, .entry_count = 2u}};
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_encoded_body encoded = {0};
        char actual[sizeof(expected)] = {0};
        size_t required = 0u;

        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, actual, sizeof(actual),
                        &required, &encoded),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(required, sizeof(expected) - 1u);
        check_equal(encoded.body_size, sizeof(expected) - 1u);
        check_equal(encoded.media_type,
                    "application/x-www-form-urlencoded");
        check_equal(actual, expected, sizeof(expected) - 1u);
    }

    it("formats every scalar lexeme and rejects non-finite numbers") {
        scxml_payload_entry entries[5] = {
            {"b", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_BOOL,
                                   .data.boolean = true}}},
            {"i", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_SINT,
                                   .data.sint = INT64_C(-7)}}},
            {"u", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_UINT,
                                   .data.uint = UINT64_MAX}}},
            {"f", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_FLOAT,
                                   .data.number = 1.5}}},
            {"s", 1u, {0}}};
        scxml_send_request request = {
            .event = "go", .event_size = 2u,
            .payload = {.kind = SCXML_PAYLOAD_NAMED,
                        .entries = entries, .entry_count = 5u}};
        const scxml_chttp_codec_limits limits = codec_limits();
        static const char expected[] =
            "_scxmleventname=go&b=true&i=-7&u=18446744073709551615&f=1.5&s=x+y";
        scxml_chttp_encoded_body encoded = {0};
        char body[sizeof(expected)] = {0};
        size_t required = 0u;

        entries[4].value = scalar_string("x y", 3u);
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(body, expected, sizeof(expected) - 1u);
        entries[3].value.scalar.data.number = INFINITY;
        memset(body, 'x', sizeof(body));
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(body[0], 'x');
    }

    it("accepts one reserved Event-name param when send event is absent") {
        static const char expected[] = "_scxmleventname=test";
        scxml_payload_entry entries[2] = {
            {"_scxmleventname", sizeof("_scxmleventname") - 1u, {0}},
            {"_scxmleventname", sizeof("_scxmleventname") - 1u, {0}}};
        scxml_send_request request = {
            .payload = {.kind = SCXML_PAYLOAD_NAMED,
                        .entries = entries, .entry_count = 1u}};
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_encoded_body encoded = {0};
        char body[sizeof(expected)] = {0};
        size_t required = 0u;

        entries[0].value = scalar_string("test", 4u);
        entries[1].value = scalar_string("other", 5u);
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(required, sizeof(expected) - 1u);
        check_equal(encoded.body_size, sizeof(expected) - 1u);
        check_equal(body, expected, sizeof(expected) - 1u);

        request.payload = (scxml_payload_view){0};
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);

        request.payload = (scxml_payload_view){
            .kind = SCXML_PAYLOAD_NAMED,
            .entries = entries, .entry_count = 2u};
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
    }

    it("formats floating point payloads independently of the numeric locale") {
        const scxml_payload_entry entry = {
            "f", 1u, {.kind = SCXML_CONTENT_SCALAR,
                        .scalar = {.kind = SCXML_PAYLOAD_VALUE_FLOAT,
                                   .data.number = 1.5}}};
        const scxml_send_request request = {
            .event = "go", .event_size = 2u,
            .payload = {.kind = SCXML_PAYLOAD_NAMED,
                        .entries = &entry, .entry_count = 1u}};
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_encoded_body encoded = {0};
        static const char *const locale_candidates[] = {
            "de-DE", "de_DE.UTF-8", "de_DE.utf8",
            "German_Germany.1252"};
        char previous_locale[128] = {0};
        char body[64] = {0};
        const char *previous = setlocale(LC_NUMERIC, NULL);
        const char *selected = NULL;
        size_t locale_index;
        size_t required = 0u;

        if (previous != NULL && strlen(previous) < sizeof(previous_locale)) {
            memcpy(previous_locale, previous, strlen(previous) + 1u);
            for (locale_index = 0u;
                 locale_index < sizeof(locale_candidates) /
                                    sizeof(locale_candidates[0]);
                 ++locale_index) {
                selected = setlocale(
                    LC_NUMERIC, locale_candidates[locale_index]);
                if (selected != NULL) break;
            }
        }
        if (selected != NULL) {
            check_equal(scxml_chttp_codec_encode(
                            &request, &limits, body, sizeof(body),
                            &required, &encoded),
                        SCXML_ADAPTER_ACCEPTED);
            check_equal(body, "_scxmleventname=go&f=1.5", required);
            check_not_null(setlocale(LC_NUMERIC, previous_locale));
        }
    }

    it("calculates capacity before writing any form byte") {
        static const char expected[] = "_scxmleventname=a+b";
        const scxml_send_request request = {
            .event = "a b", .event_size = 3u,
            .payload = {.kind = SCXML_PAYLOAD_NONE}};
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_encoded_body encoded = {0};
        char body[sizeof(expected)] = "unchanged";
        size_t required = 0u;

        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(expected) - 2u,
                        &required, &encoded),
                    SCXML_ADAPTER_FULL);
        check_equal(required, sizeof(expected) - 1u);
        check_equal(body, "unchanged");
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(expected) - 1u,
                        &required, &encoded),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(body, expected, sizeof(expected) - 1u);
    }

    it("rejects encoding bounds and invalid UTF-8 before writing") {
        scxml_payload_entry entry = {
            "name", 4u, {
                .kind = SCXML_CONTENT_SCALAR,
                .scalar = {.kind = SCXML_PAYLOAD_VALUE_STRING,
                           .data.string = {"value", 5u}}}};
        scxml_send_request request = {
            .event = "go", .event_size = 2u,
            .payload = {.kind = SCXML_PAYLOAD_NAMED,
                        .entries = &entry, .entry_count = 1u}};
        scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_encoded_body encoded = {0};
        char body[128] = "unchanged";
        size_t required = 0u;

        limits.max_event_name_bytes = 1u;
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(body, "unchanged");
        limits = codec_limits();
        limits.max_encoded_body_bytes =
            sizeof("_scxmleventname=go&name=value") - 2u;
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(required,
                    sizeof("_scxmleventname=go&name=value") - 1u);
        request.event = "\xC0";
        request.event_size = 1u;
        limits = codec_limits();
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(body, "unchanged");
        request.event = "go";
        request.event_size = 2u;
        entry.name = "_scxmleventname";
        entry.name_size = sizeof("_scxmleventname") - 1u;
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(body, "unchanged");
    }

    it("copies text XML and scalar content without form wrapping") {
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_send_request request = {
            .payload = {.kind = SCXML_PAYLOAD_CONTENT}};
        scxml_chttp_encoded_body encoded = {0};
        char body[64] = {0};
        size_t required = 0u;

        request.payload.content = (scxml_content_view){
            .kind = SCXML_CONTENT_TEXT_UTF8,
            .bytes = "hello", .byte_count = 5u};
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded), SCXML_ADAPTER_ACCEPTED);
        check_equal(encoded.media_type, "text/plain; charset=utf-8");
        check_equal(body, "hello", 5u);
        request.payload.content = (scxml_content_view){
            .kind = SCXML_CONTENT_XML_UTF8,
            .bytes = "<x/>", .byte_count = 4u};
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded), SCXML_ADAPTER_ACCEPTED);
        check_equal(encoded.media_type, "application/xml; charset=utf-8");
        check_equal(body, "<x/>", 4u);
        request.payload.content = scalar_string("plain", 5u);
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded), SCXML_ADAPTER_ACCEPTED);
        check_equal(encoded.media_type, "text/plain; charset=utf-8");
        request.payload.content.kind = SCXML_CONTENT_CMETA;
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
    }

    it("does not publish raw content metadata or bytes on failure") {
        scxml_chttp_codec_limits limits = codec_limits();
        const scxml_send_request request = {
            .payload = {.kind = SCXML_PAYLOAD_CONTENT,
                        .content = {.kind = SCXML_CONTENT_TEXT_UTF8,
                                    .bytes = "hello", .byte_count = 5u}}};
        scxml_chttp_encoded_body encoded = {
            "sentinel", 8u, 8u};
        char body[8] = "same";
        size_t required = 0u;

        limits.max_encoded_body_bytes = 4u;
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, sizeof(body),
                        &required, &encoded),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(required, (size_t)5u);
        check_null(encoded.media_type);
        check_equal(encoded.media_type_size, (size_t)0u);
        check_equal(encoded.body_size, (size_t)0u);
        check_equal(body, "same");

        limits = codec_limits();
        encoded = (scxml_chttp_encoded_body){"sentinel", 8u, 8u};
        check_equal(scxml_chttp_codec_encode(
                        &request, &limits, body, 4u,
                        &required, &encoded),
                    SCXML_ADAPTER_FULL);
        check_null(encoded.media_type);
        check_equal(encoded.media_type_size, (size_t)0u);
        check_equal(encoded.body_size, (size_t)0u);
        check_equal(body, "same");
    }

    it("decodes one reserved Event name and retains duplicate fields") {
        static const char input[] =
            "_scxmleventname=test+ready&tag=one&tag=two%21";
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_form_entry_view entries[4] = {0};
        scxml_chttp_decoded_form decoded = {0};
        char text[sizeof(input)] = {0};
        size_t required = 0u;

        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 4u, text, sizeof(text),
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_OK);
        check_equal(required, sizeof(input) - 1u);
        check_equal(decoded.event, "test ready", 10u);
        check_equal(decoded.entry_count, (size_t)2u);
        check_equal(decoded.entries[0].name, "tag", 3u);
        check_equal(decoded.entries[0].value, "one", 3u);
        check_equal(decoded.entries[1].name, "tag", 3u);
        check_equal(decoded.entries[1].value, "two!", 4u);
    }

    it("uses HTTP.POST when the reserved field is absent") {
        static const char input[] = "value=a+b";
        const scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_form_entry_view entries[1] = {0};
        scxml_chttp_decoded_form decoded = {0};
        char text[sizeof(input)] = {0};
        size_t required = 0u;

        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 1u, text, sizeof(text),
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_OK);
        check_equal(decoded.event, "HTTP.POST");
        check_equal(decoded.entries[0].value, "a b", 3u);
    }

    it("rejects malformed forms without publishing entries") {
        static const char *const invalid[] = {
            "_scxmleventname=test&bad=%G0",
            "_scxmleventname=test&bad=%2",
            "_scxmleventname=test&bad=%00",
            "_scxmleventname=%C0%80",
            "_scxmleventname=a&_scxmleventname=b",
            "=empty",
            "_scxmleventname=",
            "a=b&"};
        const scxml_chttp_codec_limits limits = codec_limits();
        size_t index;
        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            scxml_chttp_form_entry_view entries[4] = {0};
            scxml_chttp_decoded_form decoded = {0};
            char text[128] = {0};
            size_t required = 0u;
            check_equal(scxml_chttp_codec_decode_form(
                            invalid[index], strlen(invalid[index]), &limits,
                            entries, 4u, text, sizeof(text),
                            &required, &decoded),
                        SCXML_CHTTP_DECODE_BAD_REQUEST);
            check_equal(decoded.entry_count, (size_t)0u);
            check_null(decoded.entries);
        }
    }

    it("enforces body entry name value and scratch bounds") {
        static const char input[] = "_scxmleventname=test&a=12";
        scxml_chttp_codec_limits limits = codec_limits();
        scxml_chttp_form_entry_view entries[2] = {0};
        scxml_chttp_decoded_form decoded = {0};
        char text[sizeof(input)] = {0};
        size_t required = 0u;

        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 2u, text, sizeof(input) - 2u,
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        check_equal(required, sizeof(input) - 1u);
        limits.max_form_entry_count = 1u;
        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 2u, text, sizeof(text),
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        limits = codec_limits();
        limits.max_form_name_bytes = 0u;
        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 2u, text, sizeof(text),
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        limits = codec_limits();
        limits.max_form_value_bytes = 1u;
        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 2u, text, sizeof(text),
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
        limits = codec_limits();
        limits.max_encoded_body_bytes = sizeof(input) - 2u;
        check_equal(scxml_chttp_codec_decode_form(
                        input, sizeof(input) - 1u, &limits,
                        entries, 2u, text, sizeof(text),
                        &required, &decoded),
                    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED);
    }
}
