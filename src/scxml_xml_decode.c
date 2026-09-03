#include "scxml_xml_decode.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool append_utf8_codepoint(
    char *output, size_t capacity, size_t *size, uint32_t codepoint) {
    size_t required;
    if (size == NULL || codepoint == 0u ||
        codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    required = codepoint <= UINT32_C(0x7f) ? 1u
             : codepoint <= UINT32_C(0x7ff) ? 2u
             : codepoint <= UINT32_C(0xffff) ? 3u
                                             : 4u;
    if (output != NULL &&
        (*size > capacity || required > capacity - *size))
        return false;
    if (output == NULL) {
        *size += required;
    } else if (required == 1u) {
        output[(*size)++] = (char)codepoint;
    } else if (required == 2u) {
        output[(*size)++] = (char)(UINT32_C(0xc0) | (codepoint >> 6u));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    } else if (required == 3u) {
        output[(*size)++] = (char)(UINT32_C(0xe0) | (codepoint >> 12u));
        output[(*size)++] = (char)(
            UINT32_C(0x80) | ((codepoint >> 6u) & UINT32_C(0x3f)));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    } else {
        output[(*size)++] = (char)(UINT32_C(0xf0) | (codepoint >> 18u));
        output[(*size)++] = (char)(
            UINT32_C(0x80) | ((codepoint >> 12u) & UINT32_C(0x3f)));
        output[(*size)++] = (char)(
            UINT32_C(0x80) | ((codepoint >> 6u) & UINT32_C(0x3f)));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | (codepoint & UINT32_C(0x3f)));
    }
    return true;
}

static bool append_byte(
    char *output, size_t capacity, size_t *size, char value) {
    if (output != NULL && *size >= capacity) return false;
    if (output != NULL) output[*size] = value;
    ++*size;
    return true;
}

scxml_xml_decode_status scxml_xml_decode_attribute_entities(
    const char *input, size_t input_size,
    char *output, size_t output_capacity,
    size_t *out_size) {
    size_t cursor = 0u;
    size_t decoded_size = 0u;
    if ((input == NULL && input_size != 0u) || out_size == NULL ||
        (output == NULL && output_capacity != 0u))
        return SCXML_XML_DECODE_INVALID_ARGUMENT;
    while (cursor < input_size) {
        char decoded = '\0';
        size_t consumed = 0u;
        if (input[cursor] != '&') {
            if (!append_byte(
                    output, output_capacity, &decoded_size,
                    input[cursor++]))
                return SCXML_XML_DECODE_CAPACITY_EXCEEDED;
            continue;
        }
        if (input_size - cursor >= 4u &&
            memcmp(input + cursor, "&lt;", 4u) == 0) {
            decoded = '<';
            consumed = 4u;
        } else if (input_size - cursor >= 4u &&
                   memcmp(input + cursor, "&gt;", 4u) == 0) {
            decoded = '>';
            consumed = 4u;
        } else if (input_size - cursor >= 5u &&
                   memcmp(input + cursor, "&amp;", 5u) == 0) {
            decoded = '&';
            consumed = 5u;
        } else if (input_size - cursor >= 6u &&
                   memcmp(input + cursor, "&quot;", 6u) == 0) {
            decoded = '"';
            consumed = 6u;
        } else if (input_size - cursor >= 6u &&
                   memcmp(input + cursor, "&apos;", 6u) == 0) {
            decoded = '\'';
            consumed = 6u;
        }
        if (consumed != 0u) {
            if (!append_byte(
                    output, output_capacity, &decoded_size, decoded))
                return SCXML_XML_DECODE_CAPACITY_EXCEEDED;
            cursor += consumed;
            continue;
        }
        if (input_size - cursor >= 4u && input[cursor + 1u] == '#') {
            const bool hexadecimal =
                cursor + 2u < input_size &&
                (input[cursor + 2u] == 'x' ||
                 input[cursor + 2u] == 'X');
            const uint32_t base = hexadecimal ? 16u : 10u;
            size_t end = cursor + (hexadecimal ? 3u : 2u);
            uint32_t codepoint = 0u;
            bool has_digit = false;
            while (end < input_size && input[end] != ';') {
                const unsigned char ch = (unsigned char)input[end];
                uint32_t digit;
                if (ch >= '0' && ch <= '9') digit = ch - '0';
                else if (hexadecimal && ch >= 'a' && ch <= 'f')
                    digit = UINT32_C(10) + ch - 'a';
                else if (hexadecimal && ch >= 'A' && ch <= 'F')
                    digit = UINT32_C(10) + ch - 'A';
                else break;
                if (codepoint > (UINT32_C(0x10ffff) - digit) / base)
                    break;
                codepoint = codepoint * base + digit;
                has_digit = true;
                ++end;
            }
            if (!has_digit || end >= input_size || input[end] != ';' ||
                !append_utf8_codepoint(
                    output, output_capacity, &decoded_size, codepoint))
                return SCXML_XML_DECODE_INVALID_CHARACTER_REFERENCE;
            cursor = end + 1u;
            continue;
        }
        return SCXML_XML_DECODE_UNSUPPORTED_ENTITY;
    }
    *out_size = decoded_size;
    return SCXML_XML_DECODE_OK;
}
