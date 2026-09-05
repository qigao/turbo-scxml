#include "scxml_expr.h"
#include "scxml_analyze.h"
#include "scxml_location.h"

#include <query_vm.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_EXPR_DEFAULT_SOURCE_BYTES (64u * 1024u)
#define SCXML_EXPR_DEFAULT_INSTRUCTIONS 4096u
#define SCXML_EXPR_DEFAULT_OPERANDS 2048u
#define SCXML_EXPR_DEFAULT_DEPTH 64u
#define SCXML_EXPR_DEFAULT_PATH_DEPTH 32u
#define SCXML_EXPR_DEFAULT_LITERAL_BYTES (64u * 1024u)
#define SCXML_EXPR_DEFAULT_STRING_BYTES (64u * 1024u)
#define SCXML_EXPR_SINT64_UPPER_BOUND 9223372036854775808.0
#define SCXML_EXPR_UINT64_UPPER_BOUND 18446744073709551616.0

typedef enum expr_token_kind {
    EXPR_TOKEN_END = 0,
    EXPR_TOKEN_IDENT,
    EXPR_TOKEN_NUMBER,
    EXPR_TOKEN_STRING,
    EXPR_TOKEN_LPAREN,
    EXPR_TOKEN_RPAREN,
    EXPR_TOKEN_DOT,
    EXPR_TOKEN_NOT,
    EXPR_TOKEN_PLUS,
    EXPR_TOKEN_MINUS,
    EXPR_TOKEN_STAR,
    EXPR_TOKEN_SLASH,
    EXPR_TOKEN_PERCENT,
    EXPR_TOKEN_EQ,
    EXPR_TOKEN_NE,
    EXPR_TOKEN_LT,
    EXPR_TOKEN_LE,
    EXPR_TOKEN_GT,
    EXPR_TOKEN_GE,
    EXPR_TOKEN_AND,
    EXPR_TOKEN_OR,
    EXPR_TOKEN_INVALID
} expr_token_kind;

typedef enum expr_value_kind {
    EXPR_VALUE_BOOL = 1,
    EXPR_VALUE_SINT,
    EXPR_VALUE_UINT,
    EXPR_VALUE_FLOAT,
    EXPR_VALUE_STRING
} expr_value_kind;

typedef enum expr_operand_kind {
    EXPR_OPERAND_LOCATION = 1,
    EXPR_OPERAND_SUPPLEMENTAL_LOCATION,
    EXPR_OPERAND_UNRESOLVED_LOCATION,
    EXPR_OPERAND_SINT,
    EXPR_OPERAND_UINT,
    EXPR_OPERAND_FLOAT,
    EXPR_OPERAND_STRING,
    EXPR_OPERAND_STATE,
    EXPR_OPERAND_SYSTEM_EVENT_BOUND,
    EXPR_OPERAND_SYSTEM_EVENT_FIELD_BOUND,
    EXPR_OPERAND_SYSTEM_NAME_BOUND,
    EXPR_OPERAND_SYSTEM_SESSION_ID_BOUND,
    EXPR_OPERAND_SYSTEM_IO_PROCESSORS_BOUND,
    EXPR_OPERAND_SYSTEM_NAME,
    EXPR_OPERAND_SYSTEM_SESSION_ID,
    EXPR_OPERAND_SYSTEM_EVENT_NAME,
    EXPR_OPERAND_SYSTEM_EVENT_TYPE,
    EXPR_OPERAND_SYSTEM_EVENT_SEND_ID,
    EXPR_OPERAND_SYSTEM_EVENT_ORIGIN,
    EXPR_OPERAND_SYSTEM_EVENT_ORIGIN_TYPE,
    EXPR_OPERAND_SYSTEM_EVENT_INVOKE_ID,
    EXPR_OPERAND_SYSTEM_EVENT_DATA,
    EXPR_OPERAND_SYSTEM_EVENT_DATA_LOCATION,
    EXPR_OPERAND_SYSTEM_IOPROCESSOR_LOCATION
} expr_operand_kind;

typedef struct expr_token {
    expr_token_kind kind;
    size_t offset;
    size_t size;
} expr_token;

typedef struct expr_operand {
    expr_operand_kind kind;
    expr_value_kind value_kind;
    const cmeta_data_desc *data;
    const cmeta_data_field_desc *root_field;
    size_t offset;
    size_t slot;
    union {
        int64_t sint;
        uint64_t uint;
        double number;
        struct {
            const char *data;
            size_t size;
        } string;
        cflow_machine_state_id state;
        expr_operand_kind event_field;
    } value;
} expr_operand;

typedef struct scxml_expr_program_impl {
    bool external;
    const cmeta_data_desc *root;
    const scxml_scope_slot *supplemental_slots;
    size_t supplemental_count;
    qvm_instruction_t *instructions;
    expr_operand *operands;
    char *literal_storage;
    uint32_t instruction_count;
    uint32_t operand_count;
    uint32_t register_count;
    expr_value_kind result_kind;
    size_t max_path_depth;
    size_t max_string_bytes;
    qvm_limits_t qvm_limits;
    char *external_source;
    size_t external_source_size;
    scxml_expr_external_evaluate_fn external_evaluate;
} scxml_expr_program_impl;

typedef struct expr_node {
    uint16_t reg;
    expr_value_kind kind;
} expr_node;

typedef struct expr_parser {
    const char *source;
    size_t source_size;
    size_t cursor;
    expr_token token;
    const cmeta_data_desc *root;
    const scxml_scope_schema *supplemental;
    scxml_expr_resolve_state_fn resolve_state;
    void *resolve_user;
    scxml_expr_limits limits;
    scxml_expr_diagnostic *diagnostic;
    qvm_instruction_t *instructions;
    expr_operand *operands;
    size_t instruction_count;
    size_t operand_count;
    size_t literal_bytes;
    size_t retained_string_bytes;
    char *literal_storage;
    size_t literal_storage_capacity;
    size_t literal_storage_index;
    size_t expression_depth;
    size_t max_register;
    scxml_expr_path_policy path_policy;
    expr_value_kind unresolved_kind;
    scxml_expr_compile_policy policy;
    bool emit;
    scxml_expr_status status;
} expr_parser;

typedef struct expr_eval_context {
    const scxml_expr_program_impl *program;
    const unsigned char *root;
    scxml_expr_is_active_fn is_active;
    void *active_user;
    const scxml_expr_system_values *system_values;
    bool failed;
    scxml_expr_status failure_status;
} expr_eval_context;

static void expr_clear_diagnostic(
    scxml_expr_diagnostic *diagnostic) {
    if (diagnostic == NULL) return;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = SCXML_EXPR_OK;
}

static scxml_expr_status expr_report(
    scxml_expr_diagnostic *diagnostic,
    scxml_expr_status status, size_t offset,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->byte_offset = offset;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

scxml_expr_status scxml_expr_require_data_bound(
    const scxml_expr_system_values *values,
    size_t offset, size_t storage_size,
    scxml_expr_diagnostic *diagnostic) {
    bool bound = false;
    if (storage_size == 0u)
        return expr_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "CMeta data location has invalid storage");
    if (values == NULL || values->is_data_bound == NULL)
        return expr_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
    if (!values->is_data_bound(
            values->data_bound_user, offset, storage_size, &bound) ||
        !bound)
        return expr_report(
            diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, 0u,
            "CMeta data location is unbound");
    return expr_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

static bool parser_fail(expr_parser *parser,
                        scxml_expr_status status,
                        size_t offset, const char *message) {
    if (parser->status == SCXML_EXPR_OK) {
        parser->status = status;
        (void)expr_report(parser->diagnostic, status, offset, message);
    }
    return false;
}

static bool expr_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool expr_ident_start(char value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') || value == '_';
}

static bool expr_ident_continue(char value) {
    return expr_ident_start(value) || (value >= '0' && value <= '9');
}

static bool token_text_equal(const expr_parser *parser, const char *text) {
    const size_t size = strlen(text);
    return parser->token.size == size &&
           memcmp(parser->source + parser->token.offset, text, size) == 0;
}

static bool parser_event_field_kind(
    const expr_parser *parser, expr_operand_kind *out_kind) {
    if (parser == NULL || out_kind == NULL ||
        parser->token.kind != EXPR_TOKEN_IDENT)
        return false;
    if (token_text_equal(parser, "name"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_NAME;
    else if (token_text_equal(parser, "type"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_TYPE;
    else if (token_text_equal(parser, "sendid"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_SEND_ID;
    else if (token_text_equal(parser, "origin"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_ORIGIN;
    else if (token_text_equal(parser, "origintype"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_ORIGIN_TYPE;
    else if (token_text_equal(parser, "invokeid"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_INVOKE_ID;
    else if (token_text_equal(parser, "data"))
        *out_kind = EXPR_OPERAND_SYSTEM_EVENT_DATA;
    else
        return false;
    return true;
}

static uint32_t system_operand_flag(expr_operand_kind kind) {
    switch (kind) {
        case EXPR_OPERAND_SYSTEM_NAME:
        case EXPR_OPERAND_SYSTEM_NAME_BOUND:
            return SCXML_EXPR_SYSTEM_NAME;
        case EXPR_OPERAND_SYSTEM_SESSION_ID:
        case EXPR_OPERAND_SYSTEM_SESSION_ID_BOUND:
            return SCXML_EXPR_SYSTEM_SESSION_ID;
        case EXPR_OPERAND_SYSTEM_EVENT_BOUND:
            return SCXML_EXPR_SYSTEM_EVENT;
        case EXPR_OPERAND_SYSTEM_EVENT_NAME:
            return SCXML_EXPR_SYSTEM_EVENT_NAME;
        case EXPR_OPERAND_SYSTEM_EVENT_TYPE:
            return SCXML_EXPR_SYSTEM_EVENT_TYPE;
        case EXPR_OPERAND_SYSTEM_EVENT_SEND_ID:
            return SCXML_EXPR_SYSTEM_EVENT_SEND_ID;
        case EXPR_OPERAND_SYSTEM_EVENT_ORIGIN:
            return SCXML_EXPR_SYSTEM_EVENT_ORIGIN;
        case EXPR_OPERAND_SYSTEM_EVENT_ORIGIN_TYPE:
            return SCXML_EXPR_SYSTEM_EVENT_ORIGIN_TYPE;
        case EXPR_OPERAND_SYSTEM_EVENT_INVOKE_ID:
            return SCXML_EXPR_SYSTEM_EVENT_INVOKE_ID;
        case EXPR_OPERAND_SYSTEM_EVENT_DATA:
        case EXPR_OPERAND_SYSTEM_EVENT_DATA_LOCATION:
            return SCXML_EXPR_SYSTEM_EVENT_DATA;
        case EXPR_OPERAND_SYSTEM_IOPROCESSOR_LOCATION:
        case EXPR_OPERAND_SYSTEM_IO_PROCESSORS_BOUND:
            return SCXML_EXPR_SYSTEM_IOPROCESSORS;
        default:
            return 0u;
    }
}

static bool parser_allow_system_operand(
    expr_parser *parser, expr_operand_kind kind, size_t offset) {
    const uint32_t flag = system_operand_flag(kind);
    return flag != 0u &&
           (parser->policy.allowed_system_operands & flag) != 0u
        ? true
        : parser_fail(
              parser, SCXML_EXPR_UNKNOWN_LOCATION, offset,
              "system operand is unavailable in this expression profile");
}

static void parser_next(expr_parser *parser) {
    size_t begin;
    char value;
    while (parser->cursor < parser->source_size &&
           expr_space(parser->source[parser->cursor]))
        ++parser->cursor;
    begin = parser->cursor;
    parser->token.offset = begin;
    parser->token.size = 0u;
    if (begin == parser->source_size) {
        parser->token.kind = EXPR_TOKEN_END;
        return;
    }
    value = parser->source[parser->cursor++];
    if (expr_ident_start(value)) {
        while (parser->cursor < parser->source_size &&
               expr_ident_continue(parser->source[parser->cursor]))
            ++parser->cursor;
        parser->token.kind = EXPR_TOKEN_IDENT;
    } else if (value >= '0' && value <= '9') {
        bool exponent = false;
        while (parser->cursor < parser->source_size) {
            const char next = parser->source[parser->cursor];
            if (next >= '0' && next <= '9') {
                ++parser->cursor;
            } else if (next == '.' && !exponent) {
                ++parser->cursor;
            } else if ((next == 'e' || next == 'E') && !exponent) {
                exponent = true;
                ++parser->cursor;
                if (parser->cursor < parser->source_size &&
                    (parser->source[parser->cursor] == '+' ||
                     parser->source[parser->cursor] == '-'))
                    ++parser->cursor;
            } else {
                break;
            }
        }
        if (parser->cursor < parser->source_size &&
            (parser->source[parser->cursor] == 'u' ||
             parser->source[parser->cursor] == 'U'))
            ++parser->cursor;
        parser->token.kind = EXPR_TOKEN_NUMBER;
    } else if (value == '"') {
        begin = parser->cursor;
        while (parser->cursor < parser->source_size &&
               parser->source[parser->cursor] != '"' &&
               parser->source[parser->cursor] != '\\')
            ++parser->cursor;
        if (parser->cursor >= parser->source_size ||
            parser->source[parser->cursor] != '"') {
            parser->token.kind = EXPR_TOKEN_INVALID;
            parser->token.offset = begin - 1u;
            parser->token.size = parser->cursor - (begin - 1u);
            return;
        }
        parser->token.kind = EXPR_TOKEN_STRING;
        parser->token.offset = begin;
        parser->token.size = parser->cursor - begin;
        ++parser->cursor;
        return;
    } else {
        parser->token.kind = EXPR_TOKEN_INVALID;
        switch (value) {
            case '(': parser->token.kind = EXPR_TOKEN_LPAREN; break;
            case ')': parser->token.kind = EXPR_TOKEN_RPAREN; break;
            case '.': parser->token.kind = EXPR_TOKEN_DOT; break;
            case '+': parser->token.kind = EXPR_TOKEN_PLUS; break;
            case '-': parser->token.kind = EXPR_TOKEN_MINUS; break;
            case '*': parser->token.kind = EXPR_TOKEN_STAR; break;
            case '/': parser->token.kind = EXPR_TOKEN_SLASH; break;
            case '%': parser->token.kind = EXPR_TOKEN_PERCENT; break;
            case '!':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_NE;
                } else parser->token.kind = EXPR_TOKEN_NOT;
                break;
            case '=':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_EQ;
                }
                break;
            case '<':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_LE;
                } else parser->token.kind = EXPR_TOKEN_LT;
                break;
            case '>':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_GE;
                } else parser->token.kind = EXPR_TOKEN_GT;
                break;
            case '&':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '&') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_AND;
                }
                break;
            case '|':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '|') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_OR;
                }
                break;
        }
    }
    parser->token.offset = begin;
    parser->token.size = parser->cursor - begin;
}

static bool parser_add_literal_bytes(expr_parser *parser, size_t count) {
    if (count > parser->limits.max_literal_bytes - parser->literal_bytes)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "SCXML expression literal byte limit exceeded");
    parser->literal_bytes += count;
    return true;
}

static bool parser_retain_string(expr_parser *parser, size_t offset,
                                 size_t size, expr_operand *operand) {
    if (!parser_add_literal_bytes(parser, size))
        return false;
    if (size > SIZE_MAX - parser->retained_string_bytes)
        return parser_fail(parser,
                           SCXML_EXPR_LIMIT_EXCEEDED,
                           offset,
                           "CMeta retained string byte count overflow");
    operand->kind = EXPR_OPERAND_STRING;
    operand->value_kind = EXPR_VALUE_STRING;
    operand->value.string.size = size;
    if (parser->emit) {
        if (parser->literal_storage_index > parser->literal_storage_capacity ||
            size > parser->literal_storage_capacity -
                       parser->literal_storage_index)
            return parser_fail(parser,
                               SCXML_EXPR_EVALUATION_ERROR,
                               offset,
                               "CMeta string literal storage invariant failed");
        if (size != 0u) {
            operand->value.string.data =
                parser->literal_storage + parser->literal_storage_index;
            memcpy((char *)operand->value.string.data,
                   parser->source + offset, size);
        }
        parser->literal_storage_index += size;
    }
    parser->retained_string_bytes += size;
    return true;
}

static bool ioprocessor_property_follow(char value) {
    return expr_space(value) || value == ')' || value == '!' ||
           value == '+' || value == '-' || value == '*' || value == '/' ||
           value == '%' || value == '=' || value == '<' || value == '>' ||
           value == '&' || value == '|';
}

static bool ioprocessor_scan_boundary(char value) {
    return value == '(' || value == ')' || value == '!' || value == '+' ||
           value == '*' || value == '/' || value == '%' || value == '=' ||
           value == '<' || value == '>' || value == '&' || value == '|' ||
           value == '\'' || value == '"';
}

static bool parser_retain_ioprocessor_name(
    expr_parser *parser, expr_operand *operand) {
    static const char property[] = "location";
    size_t name_offset = parser->cursor;
    size_t scan;
    size_t selected_name_size = 0u;
    size_t selected_end = 0u;
    salts_xml_string_view selected_name;
    while (name_offset < parser->source_size &&
           expr_space(parser->source[name_offset]))
        ++name_offset;
    for (scan = name_offset; scan < parser->source_size; ++scan) {
        size_t property_offset;
        size_t property_end;
        size_t name_end;
        if (ioprocessor_scan_boundary(parser->source[scan])) break;
        if (parser->source[scan] != '.') continue;
        property_offset = scan + 1u;
        while (property_offset < parser->source_size &&
               expr_space(parser->source[property_offset]))
            ++property_offset;
        if (sizeof(property) - 1u >
                parser->source_size - property_offset ||
            memcmp(parser->source + property_offset,
                   property, sizeof(property) - 1u) != 0)
            continue;
        property_end = property_offset + sizeof(property) - 1u;
        if (property_end < parser->source_size &&
            !ioprocessor_property_follow(parser->source[property_end]))
            continue;
        name_end = scan;
        while (name_end > name_offset &&
               expr_space(parser->source[name_end - 1u]))
            --name_end;
        selected_name_size = name_end - name_offset;
        selected_end = property_end;
    }
    selected_name = (salts_xml_string_view){
        parser->source + name_offset, selected_name_size};
    if (selected_end == 0u ||
        !scxml_analyze_is_xml_ncname(selected_name))
        return parser_fail(
            parser, SCXML_EXPR_UNKNOWN_LOCATION, name_offset,
            "_ioprocessors requires .<name>.location");
    if (!parser_retain_string(
            parser, name_offset, selected_name_size, operand))
        return false;
    parser->cursor = selected_end;
    parser_next(parser);
    return true;
}

static bool parser_emit_instruction(expr_parser *parser, qvm_opcode_t op,
                                    uint16_t dst, uint32_t arg,
                                    uint32_t src1, uint32_t src2) {
    if (parser->instruction_count >= parser->limits.max_instructions)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "SCXML expression instruction limit exceeded");
    if (parser->emit)
        parser->instructions[parser->instruction_count] =
            (qvm_instruction_t){(uint8_t)op, 0u, dst, arg, src1, src2};
    ++parser->instruction_count;
    if ((size_t)dst + 1u > parser->max_register)
        parser->max_register = (size_t)dst + 1u;
    return true;
}

static bool parser_add_operand(expr_parser *parser, expr_operand operand,
                               uint32_t *out_index) {
    if (parser->operand_count >= parser->limits.max_operands ||
        parser->operand_count > UINT32_MAX)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "SCXML expression operand limit exceeded");
    *out_index = (uint32_t)parser->operand_count;
    if (parser->emit) parser->operands[parser->operand_count] = operand;
    ++parser->operand_count;
    return true;
}

static bool desc_scalar_kind(const cmeta_data_desc *desc,
                             expr_value_kind *out_kind) {
    size_t expected;
    if (!cmeta_data_desc_valid(desc) || desc->storage_type == NULL)
        return false;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
            if (desc->storage_type->size != sizeof(bool)) return false;
            *out_kind = EXPR_VALUE_BOOL;
            return true;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            expected = ((const cmeta_data_integer_shape *)desc->shape)->bits / 8u;
            if (desc->storage_type->size != expected) return false;
            *out_kind = desc->kind == CMETA_DATA_SINT
                            ? EXPR_VALUE_SINT : EXPR_VALUE_UINT;
            return true;
        case CMETA_DATA_FLOAT:
            expected = ((const cmeta_data_float_shape *)desc->shape)->bits / 8u;
            if (desc->storage_type->size != expected) return false;
            *out_kind = EXPR_VALUE_FLOAT;
            return true;
        case CMETA_DATA_ENUM:
            if (cmeta_data_enum_ops_of(desc) == NULL) return false;
            *out_kind = EXPR_VALUE_SINT;
            return true;
        case CMETA_DATA_STRING: {
            const cmeta_data_buffer_ops *ops =
                cmeta_data_buffer_ops_of(desc);
            if (ops == NULL ||
                ops->struct_size <
                    offsetof(cmeta_data_buffer_ops, read) +
                        sizeof(ops->read) ||
                ops->read == NULL)
                return false;
            *out_kind = EXPR_VALUE_STRING;
            return true;
        }
        default: return false;
    }
}

static const cmeta_data_field_desc *find_field_view(
    const cmeta_data_struct_shape *shape, const char *name, size_t name_size) {
    size_t index;
    if (shape == NULL || name == NULL) return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return field;
    }
    return NULL;
}

static bool parser_parse_or(expr_parser *, uint16_t, expr_node *);

static bool parser_parse_unresolved_location(
    expr_parser *parser, uint16_t target, expr_node *out,
    size_t path_begin, size_t depth) {
    expr_operand operand = {0};
    uint32_t operand_index;
    size_t path_end = path_begin;
    size_t path_size;
    while (parser->token.kind == EXPR_TOKEN_IDENT) {
        if (depth >= parser->limits.max_path_depth)
            return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "SCXML location path depth limit exceeded");
        path_end = parser->token.offset + parser->token.size;
        ++depth;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_DOT) break;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "SCXML location requires a field after '.'");
    }
    if (path_end < path_begin)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED, path_begin,
                           "SCXML unresolved path size overflow");
    path_size = path_end - path_begin;
    if (!parser_retain_string(parser, path_begin, path_size, &operand))
        return false;
    operand.kind = EXPR_OPERAND_UNRESOLVED_LOCATION;
    operand.value_kind = parser->unresolved_kind;
    out->kind = parser->unresolved_kind;
    out->reg = target;
    return parser_add_operand(parser, operand, &operand_index) &&
           parser_emit_instruction(parser, QVM_OP_LOAD_PATH, target, 0u,
                                   operand_index, 0u);
}

static bool parser_parse_supplemental_location(
    expr_parser *parser, uint16_t target, expr_node *out,
    size_t slot_index, const scxml_scope_slot *slot,
    size_t path_begin) {
    const cmeta_data_desc *desc = slot->value;
    size_t offset = 0u;
    size_t depth = 1u;
    expr_operand operand = {
        .kind = EXPR_OPERAND_SUPPLEMENTAL_LOCATION,
        .slot = slot_index};
    uint32_t operand_index;
    parser_next(parser);
    while (parser->token.kind == EXPR_TOKEN_DOT) {
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        size_t next_offset;
        if (depth >= parser->limits.max_path_depth)
            return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "SCXML location path depth limit exceeded");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "SCXML location requires a field after '.'");
        if (!cmeta_data_desc_valid(desc) || desc->storage_type == NULL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "SCXML supplemental descriptor is invalid");
        if (desc->kind != CMETA_DATA_STRUCT || desc->shape == NULL) {
            if (parser->path_policy == SCXML_EXPR_PATH_RUNTIME_MISSING)
                return parser_parse_unresolved_location(
                    parser, target, out, path_begin, depth);
            return parser_fail(parser, SCXML_EXPR_UNKNOWN_LOCATION,
                               parser->token.offset,
                               "SCXML location traverses a non-struct value");
        }
        shape = (const cmeta_data_struct_shape *)desc->shape;
        field = find_field_view(
            shape, parser->source + parser->token.offset,
            parser->token.size);
        if (field == NULL) {
            if (parser->path_policy == SCXML_EXPR_PATH_RUNTIME_MISSING)
                return parser_parse_unresolved_location(
                    parser, target, out, path_begin, depth);
            return parser_fail(parser, SCXML_EXPR_UNKNOWN_LOCATION,
                               parser->token.offset,
                               "SCXML location field is unknown");
        }
        if (!cmeta_data_desc_valid(field->value) ||
            field->value->storage_type == NULL ||
            field->offset > desc->storage_type->size ||
            field->value->storage_type->size >
                desc->storage_type->size - field->offset ||
            offset > SIZE_MAX - field->offset)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "SCXML location descriptor bounds are invalid");
        next_offset = offset + field->offset;
        if (next_offset > slot->value->storage_type->size ||
            field->value->storage_type->size >
                slot->value->storage_type->size - next_offset)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "SCXML location exceeds supplemental storage");
        offset = next_offset;
        desc = field->value;
        ++depth;
        parser_next(parser);
    }
    if (!desc_scalar_kind(desc, &out->kind))
        return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "SCXML location is not a readable scalar");
    operand.value_kind = out->kind;
    operand.data = desc;
    operand.offset = offset;
    if (!parser_add_operand(parser, operand, &operand_index) ||
        !parser_emit_instruction(parser, QVM_OP_LOAD_PATH, target, 0u,
                                 operand_index, 0u))
        return false;
    out->reg = target;
    return true;
}

static bool parser_parse_location_kind(expr_parser *parser, uint16_t target,
                                       expr_node *out,
                                       expr_operand_kind operand_kind) {
    const cmeta_data_desc *desc = parser->root;
    const size_t path_begin = parser->token.offset;
    size_t offset = 0u;
    size_t depth = 0u;
    expr_operand operand = {0};
    uint32_t operand_index;
    for (;;) {
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        size_t next_offset;
        if (depth >= parser->limits.max_path_depth)
            return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "SCXML location path depth limit exceeded");
        if (!cmeta_data_desc_valid(desc) || desc->storage_type == NULL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "SCXML location descriptor is invalid");
        if (desc->kind != CMETA_DATA_STRUCT || desc->shape == NULL) {
            if (parser->path_policy == SCXML_EXPR_PATH_RUNTIME_MISSING &&
                operand_kind == EXPR_OPERAND_LOCATION)
                return parser_parse_unresolved_location(
                    parser, target, out, path_begin, depth);
            return parser_fail(parser, SCXML_EXPR_UNKNOWN_LOCATION,
                               parser->token.offset,
                               "SCXML location traverses a non-struct value");
        }
        shape = (const cmeta_data_struct_shape *)desc->shape;
        field = find_field_view(shape, parser->source + parser->token.offset,
                                parser->token.size);
        if (field == NULL && depth == 0u &&
            operand_kind == EXPR_OPERAND_LOCATION &&
            parser->supplemental != NULL) {
            size_t slot_index = SIZE_MAX;
            const scxml_scope_slot *slot = scxml_scope_find(
                parser->supplemental,
                parser->source + parser->token.offset,
                parser->token.size, &slot_index);
            if (slot != NULL)
                return parser_parse_supplemental_location(
                    parser, target, out, slot_index, slot, path_begin);
        }
        if (field == NULL) {
            if (parser->path_policy == SCXML_EXPR_PATH_RUNTIME_MISSING &&
                operand_kind == EXPR_OPERAND_LOCATION)
                return parser_parse_unresolved_location(
                    parser, target, out, path_begin, depth);
            return parser_fail(parser, SCXML_EXPR_UNKNOWN_LOCATION,
                               parser->token.offset,
                               "SCXML location field is unknown");
        }
        if (!cmeta_data_desc_valid(field->value) ||
            field->value->storage_type == NULL ||
            field->offset > desc->storage_type->size ||
            field->value->storage_type->size >
                desc->storage_type->size - field->offset ||
            offset > SIZE_MAX - field->offset)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "SCXML location descriptor bounds are invalid");
        next_offset = offset + field->offset;
        if (next_offset > parser->root->storage_type->size ||
            field->value->storage_type->size >
                parser->root->storage_type->size - next_offset)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "SCXML location exceeds root storage");
        offset = next_offset;
        if (depth == 0u) operand.root_field = field;
        desc = field->value;
        ++depth;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_DOT) break;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "SCXML location requires a field after '.'");
    }
    if (!desc_scalar_kind(desc, &out->kind))
        return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "SCXML location is not a readable scalar");
    operand.kind = operand_kind;
    operand.value_kind = out->kind;
    operand.data = desc;
    operand.offset = offset;
    if (!parser_add_operand(parser, operand, &operand_index) ||
        !parser_emit_instruction(parser, QVM_OP_LOAD_PATH, target, 0u,
                                 operand_index, 0u))
        return false;
    out->reg = target;
    return true;
}

typedef struct event_path_match {
    const cmeta_data_desc *data;
    expr_value_kind kind;
    size_t visited;
    bool found;
    bool ambiguous;
    bool limit_exceeded;
} event_path_match;

static const cmeta_data_desc *resolve_data_path(
    const cmeta_data_desc *root, const char *path, size_t path_size,
    size_t max_depth) {
    const cmeta_data_desc *current = root;
    size_t segment_begin = 0u;
    size_t depth = 0u;
    size_t index;
    if (!cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->shape == NULL || path == NULL || path_size == 0u)
        return NULL;
    for (index = 0u; index <= path_size; ++index) {
        const bool at_end = index == path_size;
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        if (!at_end && path[index] != '.') continue;
        if (++depth > max_depth || current->kind != CMETA_DATA_STRUCT ||
            current->shape == NULL)
            return NULL;
        shape = (const cmeta_data_struct_shape *)current->shape;
        field = find_field_view(
            shape, path + segment_begin, index - segment_begin);
        if (field == NULL || !cmeta_data_desc_valid(field->value))
            return NULL;
        current = field->value;
        if (at_end) return current;
        segment_begin = index + 1u;
    }
    return NULL;
}

static void find_event_path_match(
    const cmeta_data_desc *candidate, const char *path, size_t path_size,
    size_t max_path_depth, size_t schema_depth, size_t visit_limit,
    event_path_match *match) {
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *resolved;
    expr_value_kind kind;
    size_t index;
    if (match == NULL || match->ambiguous || match->limit_exceeded ||
        schema_depth > max_path_depth || !cmeta_data_desc_valid(candidate) ||
        candidate->kind != CMETA_DATA_STRUCT || candidate->shape == NULL)
        return;
    if (match->visited >= visit_limit) {
        match->limit_exceeded = true;
        return;
    }
    ++match->visited;
    resolved = resolve_data_path(
        candidate, path, path_size, max_path_depth);
    if (resolved != NULL && desc_scalar_kind(resolved, &kind)) {
        if (!match->found) {
            match->data = resolved;
            match->kind = kind;
            match->found = true;
        } else if (match->kind != kind) {
            match->ambiguous = true;
            return;
        }
    }
    shape = (const cmeta_data_struct_shape *)candidate->shape;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_desc *field = shape->fields[index].value;
        if (cmeta_data_desc_valid(field) &&
            field->kind == CMETA_DATA_STRUCT)
            find_event_path_match(
                field, path, path_size, max_path_depth,
                schema_depth + 1u, visit_limit, match);
        if (match->ambiguous || match->limit_exceeded) return;
    }
}

static bool parser_parse_event_data_location(
    expr_parser *parser, uint16_t target, expr_node *out) {
    const size_t path_begin = parser->token.offset;
    size_t path_end = path_begin;
    size_t depth = 0u;
    size_t visit_limit;
    expr_operand operand = {0};
    event_path_match match = {0};
    uint32_t operand_index;
    while (parser->token.kind == EXPR_TOKEN_IDENT) {
        if (depth >= parser->limits.max_path_depth)
            return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "SCXML Event data path depth limit exceeded");
        path_end = parser->token.offset + parser->token.size;
        ++depth;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_DOT) break;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "SCXML Event data requires a field after '.'");
    }
    visit_limit = parser->limits.max_operands >
            SIZE_MAX / parser->limits.max_path_depth
        ? SIZE_MAX
        : parser->limits.max_operands * parser->limits.max_path_depth;
    find_event_path_match(
        parser->root, parser->source + path_begin, path_end - path_begin,
        parser->limits.max_path_depth, 0u, visit_limit, &match);
    if (match.limit_exceeded)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED, path_begin,
                           "SCXML Event data schema search limit exceeded");
    if (!match.found || match.ambiguous)
        return parser_fail(
            parser,
            match.ambiguous ? SCXML_EXPR_TYPE_MISMATCH
                            : SCXML_EXPR_UNKNOWN_LOCATION,
            path_begin,
            match.ambiguous
                ? "SCXML Event data path has ambiguous scalar types"
                : "SCXML Event data field is unknown");
    if (!parser_retain_string(
            parser, path_begin, path_end - path_begin, &operand))
        return false;
    operand.kind = EXPR_OPERAND_SYSTEM_EVENT_DATA_LOCATION;
    operand.value_kind = match.kind;
    operand.data = match.data;
    out->kind = match.kind;
    out->reg = target;
    return parser_add_operand(parser, operand, &operand_index) &&
           parser_emit_instruction(parser, QVM_OP_LOAD_PATH, target, 0u,
                                   operand_index, 0u);
}

static bool parser_parse_location(expr_parser *parser, uint16_t target,
                                  expr_node *out) {
    return parser_parse_location_kind(
        parser, target, out, EXPR_OPERAND_LOCATION);
}

static bool parse_number_operand(expr_parser *parser, expr_operand *operand) {
    char text[128];
    const char *begin = parser->source + parser->token.offset;
    size_t size = parser->token.size;
    bool unsigned_suffix = false;
    bool floating = false;
    char *end = NULL;
    size_t index;
    if (size == 0u || size >= sizeof(text))
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "numeric literal is too long");
    memcpy(text, begin, size);
    text[size] = '\0';
    if (text[size - 1u] == 'u' || text[size - 1u] == 'U') {
        unsigned_suffix = true;
        text[--size] = '\0';
    }
    for (index = 0u; index < size; ++index)
        if (text[index] == '.' || text[index] == 'e' || text[index] == 'E')
            floating = true;
    errno = 0;
    if (floating) {
        if (unsigned_suffix)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "floating literal cannot use unsigned suffix");
        operand->kind = EXPR_OPERAND_FLOAT;
        operand->value_kind = EXPR_VALUE_FLOAT;
        operand->value.number = strtod(text, &end);
    } else if (unsigned_suffix) {
        if (text[0] == '-')
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "unsigned literal cannot be negative");
        operand->kind = EXPR_OPERAND_UINT;
        operand->value_kind = EXPR_VALUE_UINT;
        operand->value.uint = strtoull(text, &end, 10);
    } else {
        operand->kind = EXPR_OPERAND_SINT;
        operand->value_kind = EXPR_VALUE_SINT;
        operand->value.sint = strtoll(text, &end, 10);
    }
    if (errno == ERANGE || end == text || *end != '\0')
        return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "numeric literal is invalid or out of range");
    return parser_add_literal_bytes(parser, parser->token.size);
}

static bool parser_parse_primary(expr_parser *parser, uint16_t target,
                                 expr_node *out) {
    if (target >= QVM_MAX_REGISTERS)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "SCXML expression register limit exceeded");
    if (parser->token.kind == EXPR_TOKEN_LPAREN) {
        bool ok;
        parser_next(parser);
        ok = parser_parse_or(parser, target, out);
        if (!ok) return false;
        if (parser->token.kind != EXPR_TOKEN_RPAREN)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "SCXML expression requires ')'");
        parser_next(parser);
        return true;
    }
    if (parser->token.kind == EXPR_TOKEN_NUMBER) {
        expr_operand operand = {0};
        uint32_t operand_index;
        if (!parse_number_operand(parser, &operand)) return false;
        out->kind = operand.value_kind;
        out->reg = target;
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        parser_next(parser);
        return true;
    }
    if (parser->token.kind == EXPR_TOKEN_STRING) {
        expr_operand operand = {0};
        uint32_t operand_index;
        if (!parser_retain_string(parser, parser->token.offset,
                                  parser->token.size, &operand))
            return false;
        out->kind = EXPR_VALUE_STRING;
        out->reg = target;
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        parser_next(parser);
        return true;
    }
    if (parser->token.kind != EXPR_TOKEN_IDENT)
        return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "SCXML expression requires a value");
    if (token_text_equal(parser, "true") || token_text_equal(parser, "false")) {
        const bool value = token_text_equal(parser, "true");
        out->kind = EXPR_VALUE_BOOL;
        out->reg = target;
        parser_next(parser);
        return parser_emit_instruction(parser,
                                       value ? QVM_OP_TRUE : QVM_OP_FALSE,
                                       target, 0u, 0u, 0u);
    }
    if (token_text_equal(parser, "_name") ||
        token_text_equal(parser, "_sessionid")) {
        const size_t offset = parser->token.offset;
        expr_operand operand = {0};
        uint32_t operand_index;
        operand.kind = token_text_equal(parser, "_name")
                           ? EXPR_OPERAND_SYSTEM_NAME
                           : EXPR_OPERAND_SYSTEM_SESSION_ID;
        if (!parser_allow_system_operand(parser, operand.kind, offset))
            return false;
        operand.value_kind = EXPR_VALUE_STRING;
        out->kind = EXPR_VALUE_STRING;
        out->reg = target;
        parser_next(parser);
        return parser_add_operand(parser, operand, &operand_index) &&
               parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target,
                                       0u, operand_index, 0u);
    }
    if (token_text_equal(parser, "isBound")) {
        const size_t offset = parser->token.offset;
        expr_operand operand = {
            .value_kind = EXPR_VALUE_BOOL};
        uint32_t operand_index;
        if (!parser->policy.allow_is_bound)
            return parser_fail(
                parser, SCXML_EXPR_UNKNOWN_LOCATION, offset,
                "isBound is unavailable in this expression profile");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_LPAREN)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "isBound requires '('");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "isBound requires a supported system variable");
        if (token_text_equal(parser, "_event"))
            operand.kind = EXPR_OPERAND_SYSTEM_EVENT_BOUND;
        else if (token_text_equal(parser, "_name"))
            operand.kind = EXPR_OPERAND_SYSTEM_NAME_BOUND;
        else if (token_text_equal(parser, "_sessionid"))
            operand.kind = EXPR_OPERAND_SYSTEM_SESSION_ID_BOUND;
        else if (token_text_equal(parser, "_ioprocessors"))
            operand.kind = EXPR_OPERAND_SYSTEM_IO_PROCESSORS_BOUND;
        else
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "isBound requires a supported system variable");
        parser_next(parser);
        if (operand.kind == EXPR_OPERAND_SYSTEM_EVENT_BOUND &&
            parser->token.kind == EXPR_TOKEN_DOT) {
            expr_operand_kind event_field;
            parser_next(parser);
            if (!parser_event_field_kind(parser, &event_field))
                return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                                   parser->token.offset,
                                   "isBound requires a supported _event field");
            operand.kind = EXPR_OPERAND_SYSTEM_EVENT_FIELD_BOUND;
            operand.value.event_field = event_field;
            parser_next(parser);
        }
        if (!parser_allow_system_operand(
                parser,
                operand.kind == EXPR_OPERAND_SYSTEM_EVENT_FIELD_BOUND
                    ? operand.value.event_field : operand.kind,
                offset))
            return false;
        if (parser->token.kind != EXPR_TOKEN_RPAREN)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "isBound accepts only one system variable");
        parser_next(parser);
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        out->reg = target;
        out->kind = EXPR_VALUE_BOOL;
        return true;
    }
    if (token_text_equal(parser, "_event")) {
        const size_t event_offset = parser->token.offset;
        expr_operand operand = {0};
        uint32_t operand_index;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_DOT) {
            return parser_fail(
                parser, SCXML_EXPR_UNKNOWN_LOCATION,
                event_offset,
                "SCXML expressions require an _event field");
        }
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT) {
            return parser_fail(
                parser, SCXML_EXPR_UNKNOWN_LOCATION,
                parser->token.offset,
                "unknown _event field");
        }
        if (!parser_event_field_kind(parser, &operand.kind))
            return parser_fail(
                parser, SCXML_EXPR_UNKNOWN_LOCATION,
                parser->token.offset, "unknown _event field");
        if (!parser_allow_system_operand(
                parser, operand.kind, event_offset))
            return false;
        operand.value_kind = EXPR_VALUE_STRING;
        out->kind = EXPR_VALUE_STRING;
        out->reg = target;
        parser_next(parser);
        if (operand.kind == EXPR_OPERAND_SYSTEM_EVENT_DATA &&
            parser->token.kind == EXPR_TOKEN_DOT) {
            parser_next(parser);
            if (parser->token.kind != EXPR_TOKEN_IDENT)
                return parser_fail(
                    parser, SCXML_EXPR_SYNTAX_ERROR,
                    parser->token.offset,
                    "_event.data requires a field after '.'");
            return parser_parse_event_data_location(parser, target, out);
        }
        return parser_add_operand(parser, operand, &operand_index) &&
               parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target,
                                       0u, operand_index, 0u);
    }
    if (token_text_equal(parser, "_ioprocessors")) {
        const size_t offset = parser->token.offset;
        expr_operand operand = {0};
        uint32_t operand_index;
        if (!parser_allow_system_operand(
                parser, EXPR_OPERAND_SYSTEM_IOPROCESSOR_LOCATION, offset))
            return false;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_DOT) {
            return parser_fail(
                parser, SCXML_EXPR_UNKNOWN_LOCATION, offset,
                "_ioprocessors requires .<name>.location");
        }
        if (!parser_retain_ioprocessor_name(parser, &operand)) return false;
        operand.kind = EXPR_OPERAND_SYSTEM_IOPROCESSOR_LOCATION;
        operand.value_kind = EXPR_VALUE_STRING;
        out->kind = EXPR_VALUE_STRING;
        out->reg = target;
        return parser_add_operand(parser, operand, &operand_index) &&
               parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target,
                                       0u, operand_index, 0u);
    }
    if (token_text_equal(parser, "In")) {
        const size_t offset = parser->token.offset;
        expr_operand operand = {0};
        uint32_t operand_index;
        size_t name_offset;
        size_t name_size;
        if (!parser->policy.allow_in)
            return parser_fail(
                parser, SCXML_EXPR_UNKNOWN_LOCATION, offset,
                "In is unavailable in this expression profile");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_LPAREN)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset, "In requires '('");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_STRING)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "In requires one quoted state name");
        name_offset = parser->token.offset;
        name_size = parser->token.size;
        if (!parser_add_literal_bytes(parser, name_size)) return false;
        operand.kind = EXPR_OPERAND_STATE;
        operand.value_kind = EXPR_VALUE_BOOL;
        if (parser->emit &&
            !parser->resolve_state(parser->resolve_user,
                                   parser->source + name_offset, name_size,
                                   &operand.value.state))
            return parser_fail(parser,
                               SCXML_EXPR_UNKNOWN_LOCATION,
                               name_offset, "In names an unknown state");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_RPAREN)
            return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "In requires exactly one state name");
        parser_next(parser);
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        out->reg = target;
        out->kind = EXPR_VALUE_BOOL;
        return true;
    }
    return parser_parse_location(parser, target, out);
}

static bool value_is_numeric(expr_value_kind kind);

static bool parser_parse_unary(expr_parser *parser, uint16_t target,
                               expr_node *out) {
    expr_token_kind operation = EXPR_TOKEN_END;
    if (target >= QVM_MAX_REGISTERS)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "SCXML expression register limit exceeded");
    if (parser->expression_depth >= parser->limits.max_expression_depth)
        return parser_fail(parser, SCXML_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "SCXML expression depth limit exceeded");
    ++parser->expression_depth;
    if (parser->token.kind == EXPR_TOKEN_NOT ||
        parser->token.kind == EXPR_TOKEN_PLUS ||
        parser->token.kind == EXPR_TOKEN_MINUS) {
        operation = parser->token.kind;
        parser_next(parser);
        if (operation == EXPR_TOKEN_MINUS &&
            parser->token.kind == EXPR_TOKEN_NUMBER &&
            parser->token.size == sizeof("9223372036854775808") - 1u &&
            memcmp(parser->source + parser->token.offset,
                   "9223372036854775808",
                   sizeof("9223372036854775808") - 1u) == 0) {
            expr_operand operand = {
                .kind = EXPR_OPERAND_SINT,
                .value_kind = EXPR_VALUE_SINT,
                .value.sint = INT64_MIN};
            uint32_t operand_index;
            if (!parser_add_literal_bytes(parser, parser->token.size) ||
                !parser_add_operand(parser, operand, &operand_index) ||
                !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target,
                                         0u, operand_index, 0u)) {
                --parser->expression_depth;
                return false;
            }
            out->reg = target;
            out->kind = EXPR_VALUE_SINT;
            parser_next(parser);
            --parser->expression_depth;
            return true;
        }
        if (!parser_parse_unary(parser, target, out)) {
            --parser->expression_depth;
            return false;
        }
    } else if (!parser_parse_primary(parser, target, out)) {
        --parser->expression_depth;
        return false;
    }
    --parser->expression_depth;
    if (operation == EXPR_TOKEN_NOT) {
        if (out->kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical not requires a Boolean operand");
        return parser_emit_instruction(parser, QVM_OP_NOT, target, 0u,
                                       target, 0u);
    }
    if (operation == EXPR_TOKEN_PLUS) {
        if (!value_is_numeric(out->kind))
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "unary plus requires a numeric operand");
        return true;
    }
    if (operation == EXPR_TOKEN_MINUS) {
        if (out->kind != EXPR_VALUE_SINT &&
            out->kind != EXPR_VALUE_FLOAT)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "unary minus requires a signed or floating operand");
        return parser_emit_instruction(parser, QVM_OP_NEG, target, 0u,
                                       target, 0u);
    }
    return true;
}

static bool value_is_numeric(expr_value_kind kind) {
    return kind == EXPR_VALUE_SINT || kind == EXPR_VALUE_UINT ||
           kind == EXPR_VALUE_FLOAT;
}

static bool parser_arithmetic_result_kind(
    expr_parser *parser, expr_token_kind operation,
    expr_value_kind left, expr_value_kind right,
    expr_value_kind *out_kind) {
    if (!value_is_numeric(left) || !value_is_numeric(right))
        return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "arithmetic requires numeric operands");
    if (operation == EXPR_TOKEN_PERCENT) {
        if (left != right ||
            (left != EXPR_VALUE_SINT && left != EXPR_VALUE_UINT))
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "remainder requires matching integral operands");
        *out_kind = left;
        return true;
    }
    if (left == EXPR_VALUE_FLOAT || right == EXPR_VALUE_FLOAT) {
        *out_kind = EXPR_VALUE_FLOAT;
        return true;
    }
    if (left != right)
        return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "signed and unsigned arithmetic cannot be mixed");
    *out_kind = left;
    return true;
}

static bool parser_parse_multiplicative(
    expr_parser *parser, uint16_t target, expr_node *out) {
    if (!parser_parse_unary(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_STAR ||
           parser->token.kind == EXPR_TOKEN_SLASH ||
           parser->token.kind == EXPR_TOKEN_PERCENT) {
        const expr_token_kind operation = parser->token.kind;
        const qvm_opcode_t opcode =
            operation == EXPR_TOKEN_STAR ? QVM_OP_MUL :
            operation == EXPR_TOKEN_SLASH ? QVM_OP_DIV : QVM_OP_MOD;
        expr_node right;
        expr_value_kind result_kind;
        parser_next(parser);
        if (!parser_parse_unary(parser, (uint16_t)(target + 1u), &right) ||
            !parser_arithmetic_result_kind(
                parser, operation, out->kind, right.kind, &result_kind) ||
            !parser_emit_instruction(parser, opcode, target, 0u,
                                     target, right.reg))
            return false;
        out->kind = result_kind;
    }
    return true;
}

static bool parser_parse_additive(
    expr_parser *parser, uint16_t target, expr_node *out) {
    if (!parser_parse_multiplicative(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_PLUS ||
           parser->token.kind == EXPR_TOKEN_MINUS) {
        const expr_token_kind operation = parser->token.kind;
        const qvm_opcode_t opcode = operation == EXPR_TOKEN_PLUS
                                        ? QVM_OP_ADD : QVM_OP_SUB;
        expr_node right;
        expr_value_kind result_kind;
        parser_next(parser);
        if (!parser_parse_multiplicative(
                parser, (uint16_t)(target + 1u), &right) ||
            !parser_arithmetic_result_kind(
                parser, operation, out->kind, right.kind, &result_kind) ||
            !parser_emit_instruction(parser, opcode, target, 0u,
                                     target, right.reg))
            return false;
        out->kind = result_kind;
    }
    return true;
}

static bool parser_parse_compare(expr_parser *parser, uint16_t target,
                                 expr_node *out) {
    expr_token_kind operation;
    expr_node right;
    uint32_t comparison;
    if (!parser_parse_additive(parser, target, out)) return false;
    operation = parser->token.kind;
    if (operation < EXPR_TOKEN_EQ || operation > EXPR_TOKEN_GE) return true;
    parser_next(parser);
    if (!parser_parse_additive(parser, (uint16_t)(target + 1u), &right))
        return false;
    if (operation == EXPR_TOKEN_EQ || operation == EXPR_TOKEN_NE) {
        if (!((out->kind == EXPR_VALUE_BOOL &&
               right.kind == EXPR_VALUE_BOOL) ||
              (out->kind == EXPR_VALUE_STRING &&
               right.kind == EXPR_VALUE_STRING) ||
              (value_is_numeric(out->kind) && value_is_numeric(right.kind))))
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "equality operands have incompatible types");
    } else if (!((out->kind == EXPR_VALUE_STRING &&
                  right.kind == EXPR_VALUE_STRING) ||
                 (value_is_numeric(out->kind) &&
                  value_is_numeric(right.kind)))) {
        return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "ordered comparison requires numeric or string operands");
    }
    comparison = operation == EXPR_TOKEN_EQ ? 0u :
                 operation == EXPR_TOKEN_NE ? 1u :
                 operation == EXPR_TOKEN_LT ? 2u :
                 operation == EXPR_TOKEN_LE ? 3u :
                 operation == EXPR_TOKEN_GT ? 4u : 5u;
    if (!parser_emit_instruction(parser, QVM_OP_CMP, target, comparison,
                                 target, right.reg))
        return false;
    out->kind = EXPR_VALUE_BOOL;
    return true;
}

static bool parser_parse_and(expr_parser *parser, uint16_t target,
                             expr_node *out) {
    if (!parser_parse_compare(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_AND) {
        expr_node right;
        size_t jump_index;
        if (out->kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical and requires Boolean operands");
        jump_index = parser->instruction_count;
        if (!parser_emit_instruction(parser, QVM_OP_JMP_FALSE, 0u, 0u,
                                     target, 0u))
            return false;
        parser_next(parser);
        if (!parser_parse_compare(parser, (uint16_t)(target + 1u), &right))
            return false;
        if (right.kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical and requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_BAND, target, 0u,
                                     target, right.reg))
            return false;
        if (parser->emit)
            parser->instructions[jump_index].arg =
                (uint32_t)parser->instruction_count;
        out->kind = EXPR_VALUE_BOOL;
    }
    return true;
}

static bool parser_parse_or(expr_parser *parser, uint16_t target,
                            expr_node *out) {
    if (!parser_parse_and(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_OR) {
        expr_node right;
        size_t jump_index;
        if (out->kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical or requires Boolean operands");
        jump_index = parser->instruction_count;
        if (!parser_emit_instruction(parser, QVM_OP_JMP_TRUE, 0u, 0u,
                                     target, 0u))
            return false;
        parser_next(parser);
        if (!parser_parse_and(parser, (uint16_t)(target + 1u), &right))
            return false;
        if (right.kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical or requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_BOR, target, 0u,
                                     target, right.reg))
            return false;
        if (parser->emit)
            parser->instructions[jump_index].arg =
                (uint32_t)parser->instruction_count;
        out->kind = EXPR_VALUE_BOOL;
    }
    return true;
}

static bool parser_run(expr_parser *parser, bool require_boolean,
                       expr_value_kind *out_kind) {
    expr_node root;
    parser_next(parser);
    if (parser->token.kind == EXPR_TOKEN_INVALID)
        return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "SCXML expression contains an invalid token");
    if (!parser_parse_or(parser, 0u, &root)) return false;
    if (parser->token.kind != EXPR_TOKEN_END)
        return parser_fail(parser, SCXML_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "SCXML expression has trailing input");
    if (require_boolean && root.kind != EXPR_VALUE_BOOL)
        return parser_fail(parser, SCXML_EXPR_TYPE_MISMATCH,
                           parser->source_size,
                           "CMeta condition result must be Boolean");
    if (out_kind != NULL) *out_kind = root.kind;
    return true;
}

scxml_expr_limits scxml_expr_default_limits(void) {
    const scxml_expr_limits limits = {
        SCXML_EXPR_DEFAULT_SOURCE_BYTES,
        SCXML_EXPR_DEFAULT_INSTRUCTIONS,
        SCXML_EXPR_DEFAULT_OPERANDS,
        SCXML_EXPR_DEFAULT_DEPTH,
        SCXML_EXPR_DEFAULT_PATH_DEPTH,
        SCXML_EXPR_DEFAULT_LITERAL_BYTES,
        SCXML_EXPR_DEFAULT_STRING_BYTES};
    return limits;
}

bool scxml_expr_limits_valid(
    const scxml_expr_limits *limits) {
    return limits != NULL && limits->max_source_bytes != 0u &&
           limits->max_instructions != 0u &&
           limits->max_instructions <= UINT32_MAX &&
           limits->max_operands != 0u &&
           limits->max_operands <= UINT32_MAX &&
           limits->max_expression_depth != 0u &&
           limits->max_expression_depth <= QVM_MAX_REGISTERS &&
           limits->max_path_depth != 0u &&
           limits->max_literal_bytes != 0u &&
           limits->max_string_bytes != 0u;
}

static void expr_program_impl_destroy(
    scxml_expr_program_impl *impl) {
    if (impl == NULL) return;
    free(impl->instructions);
    free(impl->operands);
    free(impl->literal_storage);
    free(impl->external_source);
    free(impl);
}

static scxml_expr_status expr_compile(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_path_policy path_policy,
    scxml_expr_value_kind unresolved_kind,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_compile_policy *policy_or_null,
    const scxml_expr_limits *limits_or_null,
    scxml_expr_diagnostic *diagnostic,
    bool require_boolean) {
    const scxml_expr_limits limits =
        limits_or_null != NULL ? *limits_or_null
                               : scxml_expr_default_limits();
    const scxml_expr_compile_policy policy =
        policy_or_null != NULL
            ? *policy_or_null
            : (scxml_expr_compile_policy){
                  .allowed_system_operands = SCXML_EXPR_SYSTEM_ALL,
                  .allow_in = true,
                  .allow_is_bound = true};
    expr_parser parser;
    scxml_expr_program_impl *impl = NULL;
    qvm_diagnostic_t qvm_diagnostic;
    expr_value_kind admitted_kind = (expr_value_kind)0;
    expr_value_kind emitted_kind = (expr_value_kind)0;
    size_t retained_string_bytes;
    int qvm_status;
    expr_clear_diagnostic(diagnostic);
    if (out == NULL || out->impl != NULL || source == NULL || source_size == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL || resolve_state == NULL ||
        (path_policy != SCXML_EXPR_PATH_STRICT &&
         path_policy != SCXML_EXPR_PATH_RUNTIME_MISSING) ||
        (path_policy == SCXML_EXPR_PATH_RUNTIME_MISSING &&
         (unresolved_kind < SCXML_EXPR_VALUE_BOOL ||
          unresolved_kind > SCXML_EXPR_VALUE_STRING)) ||
        (policy.allowed_system_operands & ~SCXML_EXPR_SYSTEM_ALL) != 0u ||
        !scxml_expr_limits_valid(&limits))
        return expr_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                           0u, "invalid SCXML expression compile arguments");
    if (source_size > limits.max_source_bytes)
        return expr_report(diagnostic, SCXML_EXPR_LIMIT_EXCEEDED,
                           limits.max_source_bytes,
                           "SCXML expression source byte limit exceeded");
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.source_size = source_size;
    parser.root = root;
    parser.supplemental = supplemental;
    parser.path_policy = path_policy;
    parser.unresolved_kind = (expr_value_kind)unresolved_kind;
    parser.resolve_state = resolve_state;
    parser.resolve_user = resolve_user;
    parser.policy = policy;
    parser.limits = limits;
    parser.diagnostic = diagnostic;
    parser.status = SCXML_EXPR_OK;
    if (!parser_run(&parser, require_boolean, &admitted_kind))
        return parser.status;
    retained_string_bytes = parser.retained_string_bytes;
    if (parser.instruction_count > SIZE_MAX / sizeof(*impl->instructions) ||
        parser.operand_count > SIZE_MAX / sizeof(*impl->operands))
        return expr_report(diagnostic,
                           SCXML_EXPR_LIMIT_EXCEEDED, 0u,
                           "SCXML expression storage size overflow");

    impl = (scxml_expr_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return expr_report(diagnostic,
                           SCXML_EXPR_ALLOCATION_FAILED, 0u,
                           "SCXML expression program allocation failed");
    impl->instructions = (qvm_instruction_t *)calloc(
        parser.instruction_count, sizeof(*impl->instructions));
    impl->operands = (expr_operand *)calloc(
        parser.operand_count, sizeof(*impl->operands));
    if (retained_string_bytes != 0u)
        impl->literal_storage =
            (char *)malloc(retained_string_bytes);
    if (impl->instructions == NULL ||
        (parser.operand_count != 0u && impl->operands == NULL) ||
        (retained_string_bytes != 0u &&
         impl->literal_storage == NULL)) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic,
                           SCXML_EXPR_ALLOCATION_FAILED, 0u,
                           "SCXML expression storage allocation failed");
    }

    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.source_size = source_size;
    parser.root = root;
    parser.supplemental = supplemental;
    parser.path_policy = path_policy;
    parser.unresolved_kind = (expr_value_kind)unresolved_kind;
    parser.resolve_state = resolve_state;
    parser.resolve_user = resolve_user;
    parser.policy = policy;
    parser.limits = limits;
    parser.diagnostic = diagnostic;
    parser.instructions = impl->instructions;
    parser.operands = impl->operands;
    parser.literal_storage = impl->literal_storage;
    parser.literal_storage_capacity = retained_string_bytes;
    parser.emit = true;
    parser.status = SCXML_EXPR_OK;
    if (!parser_run(&parser, require_boolean, &emitted_kind)) {
        expr_program_impl_destroy(impl);
        return parser.status;
    }
    if (parser.literal_storage_index != retained_string_bytes ||
        emitted_kind != admitted_kind) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic,
                           SCXML_EXPR_EVALUATION_ERROR, 0u,
                           "SCXML expression emission mismatched admission");
    }
    impl->root = root;
    impl->supplemental_slots = supplemental != NULL
        ? supplemental->slots : NULL;
    impl->supplemental_count = supplemental != NULL
        ? supplemental->slot_count : 0u;
    impl->instruction_count = (uint32_t)parser.instruction_count;
    impl->operand_count = (uint32_t)parser.operand_count;
    impl->register_count = (uint32_t)parser.max_register;
    impl->result_kind = emitted_kind;
    impl->max_path_depth = limits.max_path_depth;
    impl->max_string_bytes = limits.max_string_bytes;
    impl->qvm_limits = qvm_default_limits();
    impl->qvm_limits.max_instructions = impl->instruction_count;
    impl->qvm_limits.max_operands = impl->operand_count;
    impl->qvm_limits.max_regexes = 0u;
    impl->qvm_limits.max_steps = impl->instruction_count;
    qvm_status = qvm_verify_slice_ex(
        impl->instructions, impl->instruction_count, 0u,
        impl->instruction_count, impl->register_count,
        impl->operand_count, 0u, &impl->qvm_limits, &qvm_diagnostic);
    if (qvm_status != QVM_STATUS_OK) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic,
                           SCXML_EXPR_EVALUATION_ERROR,
                           qvm_diagnostic.instruction,
                           qvm_diagnostic.message);
    }
    out->impl = impl;
    expr_clear_diagnostic(diagnostic);
    return SCXML_EXPR_OK;
}

scxml_expr_status scxml_expr_compile(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, NULL,
                        SCXML_EXPR_PATH_STRICT, SCXML_EXPR_VALUE_INVALID,
                        resolve_state, resolve_user, NULL, limits,
                        diagnostic, true);
}

scxml_expr_status scxml_expr_compile_with_policy(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_compile_policy *policy,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    if (policy == NULL)
        return expr_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "missing SCXML expression compile policy");
    return expr_compile(out, source, source_size, root, NULL,
                        SCXML_EXPR_PATH_STRICT, SCXML_EXPR_VALUE_INVALID,
                        resolve_state, resolve_user, policy, limits,
                        diagnostic, true);
}

scxml_expr_status scxml_expr_compile_with_scope_policy(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_compile_policy *policy,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    if (policy == NULL)
        return expr_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "missing SCXML expression compile policy");
    return expr_compile(out, source, source_size, root, supplemental,
                        SCXML_EXPR_PATH_STRICT, SCXML_EXPR_VALUE_INVALID,
                        resolve_state, resolve_user, policy, limits,
                        diagnostic, true);
}

scxml_expr_status scxml_expr_compile_value(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, NULL,
                        SCXML_EXPR_PATH_STRICT, SCXML_EXPR_VALUE_INVALID,
                        resolve_state, resolve_user, NULL, limits,
                        diagnostic, false);
}

scxml_expr_status scxml_expr_compile_value_with_scope(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, supplemental,
                        SCXML_EXPR_PATH_STRICT, SCXML_EXPR_VALUE_INVALID,
                        resolve_state, resolve_user, NULL, limits,
                        diagnostic, false);
}

scxml_expr_status scxml_expr_compile_value_with_scope_policy(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_path_policy path_policy,
    scxml_expr_value_kind unresolved_kind,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, supplemental,
                        path_policy, unresolved_kind, resolve_state,
                        resolve_user, NULL, limits, diagnostic, false);
}

scxml_expr_status scxml_expr_compile_external(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    scxml_expr_value_kind expected_kind,
    scxml_expr_external_evaluate_fn evaluate,
    const scxml_expr_limits *limits_or_null,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_limits limits = limits_or_null != NULL
        ? *limits_or_null : scxml_expr_default_limits();
    scxml_expr_program_impl *impl;
    if (out == NULL || out->impl != NULL || source == NULL ||
        source_size == 0u || source_size > limits.max_source_bytes ||
        evaluate == NULL || !scxml_expr_limits_valid(&limits))
        return expr_report(
            diagnostic,
            source_size > limits.max_source_bytes
                ? SCXML_EXPR_LIMIT_EXCEEDED : SCXML_EXPR_INVALID_ARGUMENT,
            0u, "invalid external expression program");
    impl = (scxml_expr_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return expr_report(diagnostic, SCXML_EXPR_ALLOCATION_FAILED, 0u,
                           "external expression allocation failed");
    impl->external_source = (char *)malloc(source_size);
    if (impl->external_source == NULL) {
        free(impl);
        return expr_report(diagnostic, SCXML_EXPR_ALLOCATION_FAILED, 0u,
                           "external expression source allocation failed");
    }
    memcpy(impl->external_source, source, source_size);
    impl->external = true;
    impl->external_source_size = source_size;
    impl->external_evaluate = evaluate;
    impl->result_kind = (expr_value_kind)expected_kind;
    impl->max_string_bytes = limits.max_string_bytes;
    out->impl = impl;
    expr_clear_diagnostic(diagnostic);
    return SCXML_EXPR_OK;
}

scxml_expr_value_kind
scxml_expr_program_value_kind(
    const scxml_expr_program *program) {
    const scxml_expr_program_impl *impl =
        program != NULL
            ? (const scxml_expr_program_impl *)program->impl
            : NULL;
    return impl != NULL
               ? (scxml_expr_value_kind)impl->result_kind
               : SCXML_EXPR_VALUE_INVALID;
}

static void make_value(qvm_value_t *out, expr_value_kind kind) {
    memset(out, 0, sizeof(*out));
    out->type = (int)kind;
}

static bool read_integer(const cmeta_data_desc *desc, const void *object,
                         qvm_value_t *out) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)desc->shape)->bits;
    if (desc->kind == CMETA_DATA_SINT) {
        make_value(out, EXPR_VALUE_SINT);
        switch (bits) {
            case 8: { int8_t v; memcpy(&v, object, sizeof(v)); out->integer = v; return true; }
            case 16: { int16_t v; memcpy(&v, object, sizeof(v)); out->integer = v; return true; }
            case 32: { int32_t v; memcpy(&v, object, sizeof(v)); out->integer = v; return true; }
            case 64: memcpy(&out->integer, object, sizeof(out->integer)); return true;
        }
    } else {
        make_value(out, EXPR_VALUE_UINT);
        switch (bits) {
            case 8: { uint8_t v; memcpy(&v, object, sizeof(v)); out->uinteger = v; return true; }
            case 16: { uint16_t v; memcpy(&v, object, sizeof(v)); out->uinteger = v; return true; }
            case 32: { uint32_t v; memcpy(&v, object, sizeof(v)); out->uinteger = v; return true; }
            case 64: memcpy(&out->uinteger, object, sizeof(out->uinteger)); return true;
        }
    }
    return false;
}

static bool read_location_at(const expr_eval_context *context,
                             const expr_operand *operand,
                             const unsigned char *root,
                             qvm_value_t *out) {
    const void *object;
    if (root == NULL) return false;
    object = root + operand->offset;
    switch (operand->data->kind) {
        case CMETA_DATA_BOOL: {
            bool value;
            memcpy(&value, object, sizeof(value));
            make_value(out, EXPR_VALUE_BOOL);
            out->boolean = value;
            return true;
        }
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            return read_integer(operand->data, object, out);
        case CMETA_DATA_FLOAT: {
            const uint8_t bits =
                ((const cmeta_data_float_shape *)operand->data->shape)->bits;
            make_value(out, EXPR_VALUE_FLOAT);
            if (bits == 32u) {
                float value;
                memcpy(&value, object, sizeof(value));
                out->number = value;
                return true;
            }
            if (bits == 64u) {
                memcpy(&out->number, object, sizeof(out->number));
                return true;
            }
            return false;
        }
        case CMETA_DATA_ENUM:
            make_value(out, EXPR_VALUE_SINT);
            return cmeta_data_enum_read(operand->data, object,
                                        &out->integer) == CMETA_OK;
        case CMETA_DATA_STRING: {
            const unsigned char *data = NULL;
            size_t size = 0u;
            if (cmeta_data_buffer_read(
                    operand->data, object,
                    context->program->max_string_bytes,
                    &data, &size) != CMETA_OK)
                return false;
            make_value(out, EXPR_VALUE_STRING);
            out->str = (const char *)data;
            out->length = size;
            return true;
        }
        default: return false;
    }
}

static const scxml_expr_string_view *system_event_field_view(
    const scxml_expr_system_values *values, expr_operand_kind kind) {
    if (values == NULL) return NULL;
    switch (kind) {
        case EXPR_OPERAND_SYSTEM_EVENT_NAME: return &values->event_name;
        case EXPR_OPERAND_SYSTEM_EVENT_TYPE: return &values->event_type;
        case EXPR_OPERAND_SYSTEM_EVENT_SEND_ID: return &values->event_send_id;
        case EXPR_OPERAND_SYSTEM_EVENT_ORIGIN: return &values->event_origin;
        case EXPR_OPERAND_SYSTEM_EVENT_ORIGIN_TYPE:
            return &values->event_origin_type;
        case EXPR_OPERAND_SYSTEM_EVENT_INVOKE_ID:
            return &values->event_invoke_id;
        case EXPR_OPERAND_SYSTEM_EVENT_DATA: return &values->event_data;
        default: return NULL;
    }
}

static bool read_event_data_location(
    expr_eval_context *context, const expr_operand *operand,
    qvm_value_t *out) {
    scxml_location location = {0};
    expr_operand resolved;
    expr_value_kind kind;
    if (context == NULL || operand == NULL || out == NULL ||
        context->system_values == NULL ||
        context->system_values->event_data_object == NULL ||
        operand->value.string.data == NULL ||
        operand->value.string.size == 0u ||
        scxml_location_compile(
            &location, operand->value.string.data,
            operand->value.string.size,
            context->system_values->event_data_schema,
            context->program->max_path_depth, false, NULL) != SCXML_EXPR_OK ||
        location.value == NULL ||
        !desc_scalar_kind(location.value, &kind) ||
        kind != operand->value_kind)
        return false;
    resolved = *operand;
    resolved.data = location.value;
    resolved.offset = location.offset;
    return read_location_at(
        context, &resolved,
        (const unsigned char *)context->system_values->event_data_object,
        out);
}

static int expr_resolve(void *user, uint32_t index, qvm_value_t *out) {
    expr_eval_context *context = (expr_eval_context *)user;
    const expr_operand *operand;
    bool active;
    if (context == NULL || out == NULL ||
        index >= context->program->operand_count) {
        if (context != NULL) context->failed = true;
        return 0;
    }
    operand = &context->program->operands[index];
    switch (operand->kind) {
        case EXPR_OPERAND_LOCATION: {
            scxml_expr_status bound_status;
            if (operand->data == NULL ||
                operand->data->storage_type == NULL) {
                context->failed = true;
                return 0;
            }
            bound_status = scxml_expr_require_data_bound(
                context->system_values, operand->offset,
                operand->data->storage_type->size, NULL);
            if (bound_status != SCXML_EXPR_OK) {
                context->failed = true;
                context->failure_status = bound_status;
                return 0;
            }
            if (!read_location_at(context, operand, context->root, out)) {
                context->failed = true;
                return 0;
            }
            return 1;
        }
        case EXPR_OPERAND_SUPPLEMENTAL_LOCATION: {
            const cmeta_data_desc *slot_value = NULL;
            const void *slot_object = NULL;
            if (context->system_values == NULL ||
                context->system_values->supplemental == NULL ||
                context->system_values->supplemental->schema == NULL ||
                context->system_values->supplemental->schema->slots !=
                    context->program->supplemental_slots ||
                context->system_values->supplemental->schema->slot_count !=
                    context->program->supplemental_count ||
                !scxml_scope_view_read(
                    context->system_values->supplemental, operand->slot,
                    &slot_value, &slot_object) ||
                operand->data == NULL ||
                operand->data->storage_type == NULL ||
                operand->offset > slot_value->storage_type->size ||
                operand->data->storage_type->size >
                    slot_value->storage_type->size - operand->offset ||
                !read_location_at(
                    context, operand,
                    (const unsigned char *)slot_object, out)) {
                context->failed = true;
                if (context->system_values != NULL &&
                    context->system_values->supplemental != NULL &&
                    context->system_values->supplemental->schema != NULL &&
                    operand->slot < context->system_values->supplemental->schema->slot_count &&
                    context->system_values->supplemental->bound != NULL &&
                    context->system_values->supplemental->bound[operand->slot] == 0u)
                    context->failure_status = SCXML_EXPR_UNKNOWN_LOCATION;
                return 0;
            }
            return 1;
        }
        case EXPR_OPERAND_UNRESOLVED_LOCATION:
            context->failed = true;
            context->failure_status = SCXML_EXPR_UNKNOWN_LOCATION;
            return 0;
        case EXPR_OPERAND_SYSTEM_EVENT_DATA_LOCATION:
            if (!read_event_data_location(context, operand, out)) {
                context->failed = true;
                return 0;
            }
            return 1;
        case EXPR_OPERAND_SINT:
            make_value(out, EXPR_VALUE_SINT);
            out->integer = operand->value.sint;
            return 1;
        case EXPR_OPERAND_UINT:
            make_value(out, EXPR_VALUE_UINT);
            out->uinteger = operand->value.uint;
            return 1;
        case EXPR_OPERAND_FLOAT:
            make_value(out, EXPR_VALUE_FLOAT);
            out->number = operand->value.number;
            return 1;
        case EXPR_OPERAND_STRING:
            make_value(out, EXPR_VALUE_STRING);
            out->str = operand->value.string.data;
            out->length = operand->value.string.size;
            return 1;
        case EXPR_OPERAND_STATE:
            if (!context->is_active(context->active_user,
                                    operand->value.state, &active)) {
                context->failed = true;
                return 0;
            }
            make_value(out, EXPR_VALUE_BOOL);
            out->boolean = active;
            return 1;
        case EXPR_OPERAND_SYSTEM_EVENT_BOUND:
        case EXPR_OPERAND_SYSTEM_EVENT_FIELD_BOUND:
        case EXPR_OPERAND_SYSTEM_NAME_BOUND:
        case EXPR_OPERAND_SYSTEM_SESSION_ID_BOUND:
        case EXPR_OPERAND_SYSTEM_IO_PROCESSORS_BOUND: {
            const scxml_expr_string_view *view;
            if (context->system_values == NULL) {
                context->failed = true;
                return 0;
            }
            if (operand->kind == EXPR_OPERAND_SYSTEM_EVENT_FIELD_BOUND) {
                const bool event_bound =
                    context->system_values->event_name.data != NULL;
                const expr_operand_kind field = operand->value.event_field;
                view = system_event_field_view(context->system_values, field);
                if (view == NULL) {
                    context->failed = true;
                    return 0;
                }
                make_value(out, EXPR_VALUE_BOOL);
                if (field == EXPR_OPERAND_SYSTEM_EVENT_DATA) {
                    const bool structured_data_bound =
                        context->system_values->event_data_schema != NULL &&
                        context->system_values->event_data_object != NULL;
                    out->boolean = event_bound &&
                        (view->data != NULL || structured_data_bound);
                } else {
                    out->boolean = event_bound && view->data != NULL;
                }
                return 1;
            }
            if (operand->kind == EXPR_OPERAND_SYSTEM_EVENT_BOUND)
                view = &context->system_values->event_name;
            else if (operand->kind == EXPR_OPERAND_SYSTEM_NAME_BOUND)
                view = &context->system_values->name;
            else if (operand->kind == EXPR_OPERAND_SYSTEM_SESSION_ID_BOUND)
                view = &context->system_values->session_id;
            else {
                make_value(out, EXPR_VALUE_BOOL);
                out->boolean =
                    context->system_values->ioprocessors != NULL &&
                    context->system_values->ioprocessor_count != 0u;
                return 1;
            }
            make_value(out, EXPR_VALUE_BOOL);
            out->boolean = view->data != NULL;
            return 1;
        }
        case EXPR_OPERAND_SYSTEM_NAME:
        case EXPR_OPERAND_SYSTEM_SESSION_ID:
        case EXPR_OPERAND_SYSTEM_EVENT_NAME:
        case EXPR_OPERAND_SYSTEM_EVENT_TYPE:
        case EXPR_OPERAND_SYSTEM_EVENT_SEND_ID:
        case EXPR_OPERAND_SYSTEM_EVENT_ORIGIN:
        case EXPR_OPERAND_SYSTEM_EVENT_ORIGIN_TYPE:
        case EXPR_OPERAND_SYSTEM_EVENT_INVOKE_ID:
        case EXPR_OPERAND_SYSTEM_EVENT_DATA: {
            const scxml_expr_string_view *view;
            if (context->system_values == NULL) {
                context->failed = true;
                return 0;
            }
            if (operand->kind == EXPR_OPERAND_SYSTEM_NAME) {
                view = &context->system_values->name;
            } else if (operand->kind == EXPR_OPERAND_SYSTEM_SESSION_ID) {
                view = &context->system_values->session_id;
            } else {
                view = system_event_field_view(
                    context->system_values, operand->kind);
                if (view == NULL) {
                    context->failed = true;
                    return 0;
                }
            }
            if (view->data == NULL ||
                view->size > context->program->max_string_bytes) {
                context->failed = true;
                return 0;
            }
            make_value(out, EXPR_VALUE_STRING);
            out->str = view->data;
            out->length = view->size;
            return 1;
        }
        case EXPR_OPERAND_SYSTEM_IOPROCESSOR_LOCATION: {
            const scxml_ioprocessor_descriptor *row = NULL;
            size_t row_index;
            if (context->system_values == NULL ||
                (context->system_values->ioprocessor_count != 0u &&
                 context->system_values->ioprocessors == NULL)) {
                context->failed = true;
                return 0;
            }
            for (row_index = 0u;
                 row_index < context->system_values->ioprocessor_count;
                 ++row_index) {
                const scxml_ioprocessor_descriptor *candidate =
                    &context->system_values->ioprocessors[row_index];
                if (candidate->name_size == operand->value.string.size &&
                    (candidate->name_size == 0u ||
                     memcmp(candidate->name, operand->value.string.data,
                            candidate->name_size) == 0)) {
                    row = candidate;
                    break;
                }
            }
            if (row == NULL) {
                context->failed = true;
                context->failure_status = SCXML_EXPR_UNKNOWN_LOCATION;
                return 0;
            }
            if (row->location == NULL ||
                row->location_size > context->program->max_string_bytes) {
                context->failed = true;
                return 0;
            }
            make_value(out, EXPR_VALUE_STRING);
            out->str = row->location;
            out->length = row->location_size;
            return 1;
        }
    }
    context->failed = true;
    return 0;
}

static int expr_truthy(void *user, const qvm_value_t *value) {
    (void)user;
    return value != NULL && value->type == EXPR_VALUE_BOOL &&
           value->boolean != 0;
}

static void compare_sint_float(int64_t integer, double number,
                               int *out_order, bool *out_unordered) {
    int64_t truncated;
    double integral;
    if (isnan(number)) {
        *out_unordered = true;
        *out_order = 0;
        return;
    }
    if (number >= SCXML_EXPR_SINT64_UPPER_BOUND) {
        *out_order = -1;
        return;
    }
    if (number < -SCXML_EXPR_SINT64_UPPER_BOUND) {
        *out_order = 1;
        return;
    }
    truncated = (int64_t)number;
    if (integer < truncated) {
        *out_order = -1;
        return;
    }
    if (integer > truncated) {
        *out_order = 1;
        return;
    }
    integral = (double)truncated;
    *out_order = integral < number ? -1 : integral > number ? 1 : 0;
}

static void compare_uint_float(uint64_t integer, double number,
                               int *out_order, bool *out_unordered) {
    uint64_t truncated;
    double integral;
    if (isnan(number)) {
        *out_unordered = true;
        *out_order = 0;
        return;
    }
    if (number >= SCXML_EXPR_UINT64_UPPER_BOUND) {
        *out_order = -1;
        return;
    }
    if (number < 0.0) {
        *out_order = 1;
        return;
    }
    truncated = (uint64_t)number;
    if (integer < truncated) {
        *out_order = -1;
        return;
    }
    if (integer > truncated) {
        *out_order = 1;
        return;
    }
    integral = (double)truncated;
    *out_order = integral < number ? -1 : integral > number ? 1 : 0;
}

static bool numeric_compare(const qvm_value_t *left,
                            const qvm_value_t *right, int *out_order,
                            bool *out_unordered) {
    *out_unordered = false;
    if ((left->type != EXPR_VALUE_SINT &&
         left->type != EXPR_VALUE_UINT &&
         left->type != EXPR_VALUE_FLOAT) ||
        (right->type != EXPR_VALUE_SINT &&
         right->type != EXPR_VALUE_UINT &&
         right->type != EXPR_VALUE_FLOAT))
        return false;
    if (left->type == EXPR_VALUE_FLOAT &&
        right->type == EXPR_VALUE_FLOAT) {
        if (isnan(left->number) || isnan(right->number)) {
            *out_unordered = true;
            *out_order = 0;
        } else {
            *out_order = left->number < right->number ? -1 :
                         left->number > right->number ? 1 : 0;
        }
        return true;
    }
    if (right->type == EXPR_VALUE_FLOAT) {
        if (left->type == EXPR_VALUE_SINT)
            compare_sint_float(left->integer, right->number,
                               out_order, out_unordered);
        else
            compare_uint_float(left->uinteger, right->number,
                               out_order, out_unordered);
        return true;
    }
    if (left->type == EXPR_VALUE_FLOAT) {
        if (right->type == EXPR_VALUE_SINT)
            compare_sint_float(right->integer, left->number,
                               out_order, out_unordered);
        else
            compare_uint_float(right->uinteger, left->number,
                               out_order, out_unordered);
        *out_order = -*out_order;
        return true;
    }
    if (left->type == EXPR_VALUE_SINT && right->type == EXPR_VALUE_SINT) {
        *out_order = left->integer < right->integer ? -1 :
                     left->integer > right->integer ? 1 : 0;
        return true;
    }
    if (left->type == EXPR_VALUE_UINT && right->type == EXPR_VALUE_UINT) {
        *out_order = left->uinteger < right->uinteger ? -1 :
                     left->uinteger > right->uinteger ? 1 : 0;
        return true;
    }
    if (left->type == EXPR_VALUE_SINT) {
        *out_order = left->integer < 0 ? -1 :
                     (uint64_t)left->integer < right->uinteger ? -1 :
                     (uint64_t)left->integer > right->uinteger ? 1 : 0;
        return true;
    }
    if (right->integer < 0) *out_order = 1;
    else *out_order = left->uinteger < (uint64_t)right->integer ? -1 :
                      left->uinteger > (uint64_t)right->integer ? 1 : 0;
    return true;
}

static bool arithmetic_opcode(qvm_opcode_t op) {
    return op == QVM_OP_ADD || op == QVM_OP_SUB ||
           op == QVM_OP_MUL || op == QVM_OP_DIV || op == QVM_OP_MOD;
}

static bool checked_sint_multiply(int64_t left, int64_t right,
                                  int64_t *out) {
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left))
            return false;
    } else if (left < 0) {
        if ((right > 0 && left < INT64_MIN / right) ||
            (right < 0 && right < INT64_MAX / left))
            return false;
    }
    *out = left * right;
    return true;
}

static bool evaluate_sint_arithmetic(qvm_opcode_t op,
                                     int64_t left, int64_t right,
                                     int64_t *out) {
    switch (op) {
        case QVM_OP_ADD:
            if ((right > 0 && left > INT64_MAX - right) ||
                (right < 0 && left < INT64_MIN - right))
                return false;
            *out = left + right;
            return true;
        case QVM_OP_SUB:
            if ((right > 0 && left < INT64_MIN + right) ||
                (right < 0 && left > INT64_MAX + right))
                return false;
            *out = left - right;
            return true;
        case QVM_OP_MUL:
            return checked_sint_multiply(left, right, out);
        case QVM_OP_DIV:
            if (right == 0 || (left == INT64_MIN && right == -1))
                return false;
            *out = left / right;
            return true;
        case QVM_OP_MOD:
            if (right == 0 || (left == INT64_MIN && right == -1))
                return false;
            *out = left % right;
            return true;
        default: return false;
    }
}

static bool evaluate_uint_arithmetic(qvm_opcode_t op,
                                     uint64_t left, uint64_t right,
                                     uint64_t *out) {
    switch (op) {
        case QVM_OP_ADD:
            if (left > UINT64_MAX - right) return false;
            *out = left + right;
            return true;
        case QVM_OP_SUB:
            if (left < right) return false;
            *out = left - right;
            return true;
        case QVM_OP_MUL:
            if (right != 0u && left > UINT64_MAX / right) return false;
            *out = left * right;
            return true;
        case QVM_OP_DIV:
            if (right == 0u) return false;
            *out = left / right;
            return true;
        case QVM_OP_MOD:
            if (right == 0u) return false;
            *out = left % right;
            return true;
        default: return false;
    }
}

static bool numeric_as_double(const qvm_value_t *value, double *out) {
    if (value->type == EXPR_VALUE_FLOAT)
        *out = value->number;
    else if (value->type == EXPR_VALUE_SINT)
        *out = (double)value->integer;
    else if (value->type == EXPR_VALUE_UINT)
        *out = (double)value->uinteger;
    else
        return false;
    return true;
}

static bool evaluate_float_arithmetic(qvm_opcode_t op,
                                      const qvm_value_t *left,
                                      const qvm_value_t *right,
                                      double *out) {
    double left_number;
    double right_number;
    if (op == QVM_OP_MOD ||
        !numeric_as_double(left, &left_number) ||
        !numeric_as_double(right, &right_number) ||
        ((op == QVM_OP_DIV) && right_number == 0.0))
        return false;
    switch (op) {
        case QVM_OP_ADD: *out = left_number + right_number; break;
        case QVM_OP_SUB: *out = left_number - right_number; break;
        case QVM_OP_MUL: *out = left_number * right_number; break;
        case QVM_OP_DIV: *out = left_number / right_number; break;
        default: return false;
    }
    return isfinite(*out) != 0;
}

static int expr_arithmetic_binary(qvm_opcode_t op,
                                  const qvm_value_t *left,
                                  const qvm_value_t *right,
                                  qvm_value_t *out) {
    if (left->type == EXPR_VALUE_SINT &&
        right->type == EXPR_VALUE_SINT) {
        int64_t result;
        if (!evaluate_sint_arithmetic(
                op, left->integer, right->integer, &result))
            return 0;
        make_value(out, EXPR_VALUE_SINT);
        out->integer = result;
        return 1;
    }
    if (left->type == EXPR_VALUE_UINT &&
        right->type == EXPR_VALUE_UINT) {
        uint64_t result;
        if (!evaluate_uint_arithmetic(
                op, left->uinteger, right->uinteger, &result))
            return 0;
        make_value(out, EXPR_VALUE_UINT);
        out->uinteger = result;
        return 1;
    }
    if (left->type == EXPR_VALUE_FLOAT ||
        right->type == EXPR_VALUE_FLOAT) {
        double result;
        if (!evaluate_float_arithmetic(op, left, right, &result)) return 0;
        make_value(out, EXPR_VALUE_FLOAT);
        out->number = result;
        return 1;
    }
    return 0;
}

static int expr_binary(void *user, qvm_opcode_t op, uint32_t arg,
                       const qvm_value_t *left, const qvm_value_t *right,
                       qvm_value_t *out) {
    int order = 0;
    bool unordered = false;
    bool result;
    (void)user;
    if (arithmetic_opcode(op))
        return expr_arithmetic_binary(op, left, right, out);
    if (op == QVM_OP_BAND || op == QVM_OP_BOR) {
        bool logical;
        if (left->type != EXPR_VALUE_BOOL || right->type != EXPR_VALUE_BOOL)
            return 0;
        logical = op == QVM_OP_BAND
                      ? (left->boolean && right->boolean)
                      : (left->boolean || right->boolean);
        make_value(out, EXPR_VALUE_BOOL);
        out->boolean = logical;
        return 1;
    }
    if (op != QVM_OP_CMP || arg > 5u) return 0;
    if (left->type == EXPR_VALUE_BOOL && right->type == EXPR_VALUE_BOOL) {
        if (arg > 1u) return 0;
        order = left->boolean == right->boolean ? 0 :
                left->boolean ? 1 : -1;
    } else if (left->type == EXPR_VALUE_STRING &&
               right->type == EXPR_VALUE_STRING) {
        const size_t common = left->length < right->length
                                  ? left->length : right->length;
        order = common != 0u ? memcmp(left->str, right->str, common) : 0;
        if (order == 0)
            order = left->length < right->length ? -1 :
                    left->length > right->length ? 1 : 0;
    } else if (!numeric_compare(left, right, &order, &unordered)) {
        return 0;
    }
    result = arg == 0u ? (!unordered && order == 0) :
             arg == 1u ? (unordered || order != 0) :
             arg == 2u ? (!unordered && order < 0) :
             arg == 3u ? (!unordered && order <= 0) :
             arg == 4u ? (!unordered && order > 0) :
                         (!unordered && order >= 0);
    make_value(out, EXPR_VALUE_BOOL);
    out->boolean = result;
    return 1;
}

static int expr_unary(void *user, qvm_opcode_t op,
                      const qvm_value_t *input, qvm_value_t *out) {
    (void)user;
    if (op != QVM_OP_NEG || input == NULL || out == NULL) return 0;
    if (input->type == EXPR_VALUE_SINT) {
        const int64_t value = input->integer;
        if (value == INT64_MIN) return 0;
        make_value(out, EXPR_VALUE_SINT);
        out->integer = -value;
        return 1;
    }
    if (input->type == EXPR_VALUE_FLOAT) {
        const double result = -input->number;
        if (!isfinite(result)) return 0;
        make_value(out, EXPR_VALUE_FLOAT);
        out->number = result;
        return 1;
    }
    return 0;
}

static void expr_make_invalid(void *user, qvm_value_t *out) {
    (void)user;
    memset(out, 0, sizeof(*out));
}

static void expr_make_bool(void *user, int value, qvm_value_t *out) {
    (void)user;
    make_value(out, EXPR_VALUE_BOOL);
    out->boolean = value != 0;
}

static void expr_make_number(void *user, double value, qvm_value_t *out) {
    (void)user;
    make_value(out, EXPR_VALUE_FLOAT);
    out->number = value;
}

static void expr_make_string(void *user, const char *value, size_t size,
                             qvm_value_t *out) {
    (void)user;
    make_value(out, EXPR_VALUE_STRING);
    out->str = value;
    out->length = size;
}

static scxml_expr_status expr_evaluate(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    qvm_value_t *out_value,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_program_impl *impl =
        program != NULL
            ? (const scxml_expr_program_impl *)program->impl
            : NULL;
    expr_eval_context context;
    qvm_exec_ops_t ops = {0};
    qvm_value_t result;
    qvm_diagnostic_t qvm_diagnostic;
    int status;
    expr_clear_diagnostic(diagnostic);
    if (impl == NULL || root_object == NULL || is_active == NULL ||
        out_value == NULL)
        return expr_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                           0u, "invalid SCXML expression evaluation arguments");
    context.program = impl;
    context.root = (const unsigned char *)root_object;
    context.is_active = is_active;
    context.active_user = active_user;
    context.system_values = system_values;
    context.failed = false;
    context.failure_status = SCXML_EXPR_OK;
    ops.resolve = expr_resolve;
    ops.truthy = expr_truthy;
    ops.binary = expr_binary;
    ops.unary = expr_unary;
    ops.make_invalid = expr_make_invalid;
    ops.make_bool = expr_make_bool;
    ops.make_number = expr_make_number;
    ops.make_string = expr_make_string;
    status = qvm_execute_ex(impl->instructions, impl->instruction_count,
                            0u, impl->instruction_count, &ops, &context,
                            NULL, &result, &impl->qvm_limits, &qvm_diagnostic);
    if (context.failed || status != QVM_STATUS_OK ||
        result.type != (int)impl->result_kind)
        return expr_report(diagnostic,
                           context.failed &&
                                   context.failure_status != SCXML_EXPR_OK
                               ? context.failure_status
                               : SCXML_EXPR_EVALUATION_ERROR,
                           qvm_diagnostic.instruction,
                           context.failed
                               ? "SCXML expression operand resolution failed"
                               : status == QVM_STATUS_OK
                               ? "SCXML expression result type mismatched program"
                               : qvm_diagnostic.message);
    *out_value = result;
    return SCXML_EXPR_OK;
}

static scxml_expr_status expr_evaluate_condition(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    bool *out_value,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_program_impl *impl =
        program != NULL
            ? (const scxml_expr_program_impl *)program->impl
            : NULL;
    qvm_value_t result;
    scxml_expr_status status;
    bool value;
    if (impl == NULL || impl->result_kind != EXPR_VALUE_BOOL ||
        out_value == NULL) {
        expr_clear_diagnostic(diagnostic);
        return expr_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                           0u, "invalid CMeta condition evaluation arguments");
    }
    if (impl->external) {
        scxml_expr_value external = {0};
        status = impl->external_evaluate(
            impl->external_source, impl->external_source_size,
            SCXML_EXPR_VALUE_BOOL, root_object, is_active, active_user,
            system_values, &external, diagnostic);
        if (status != SCXML_EXPR_OK) return status;
        if (external.kind != SCXML_EXPR_VALUE_BOOL)
            return expr_report(diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
                               "external condition did not produce bool");
        *out_value = external.data.boolean;
        return SCXML_EXPR_OK;
    }
    status = expr_evaluate(program, root_object, is_active, active_user,
                           system_values, &result, diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    value = result.boolean != 0;
    *out_value = value;
    return SCXML_EXPR_OK;
}

scxml_expr_status scxml_expr_evaluate(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    bool *out_value,
    scxml_expr_diagnostic *diagnostic) {
    return expr_evaluate_condition(program, root_object, is_active,
                                   active_user, NULL, out_value, diagnostic);
}

scxml_expr_status scxml_expr_evaluate_with_system(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    bool *out_value,
    scxml_expr_diagnostic *diagnostic) {
    return expr_evaluate_condition(program, root_object, is_active,
                                   active_user, system_values, out_value,
                                   diagnostic);
}

static scxml_expr_status expr_evaluate_public_value(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic) {
    qvm_value_t result;
    scxml_expr_value value;
    scxml_expr_status status;
    if (out_value == NULL) {
        expr_clear_diagnostic(diagnostic);
        return expr_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                           0u, "invalid CMeta value evaluation arguments");
    }
    {
        const scxml_expr_program_impl *impl =
            program != NULL
                ? (const scxml_expr_program_impl *)program->impl : NULL;
        if (impl != NULL && impl->external)
            return impl->external_evaluate(
                impl->external_source, impl->external_source_size,
                (scxml_expr_value_kind)impl->result_kind,
                root_object, is_active, active_user, system_values,
                out_value, diagnostic);
    }
    status = expr_evaluate(program, root_object, is_active, active_user,
                           system_values, &result, diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    memset(&value, 0, sizeof(value));
    value.kind = (scxml_expr_value_kind)result.type;
    switch (value.kind) {
        case SCXML_EXPR_VALUE_BOOL:
            value.data.boolean = result.boolean != 0;
            break;
        case SCXML_EXPR_VALUE_SINT:
            value.data.sint = result.integer;
            break;
        case SCXML_EXPR_VALUE_UINT:
            value.data.uint = result.uinteger;
            break;
        case SCXML_EXPR_VALUE_FLOAT:
            value.data.number = result.number;
            break;
        case SCXML_EXPR_VALUE_STRING:
            value.data.string.data = result.str;
            value.data.string.size = result.length;
            break;
        default:
            return expr_report(diagnostic,
                               SCXML_EXPR_EVALUATION_ERROR, 0u,
                               "SCXML expression produced an invalid scalar");
    }
    *out_value = value;
    return SCXML_EXPR_OK;
}

scxml_expr_status scxml_expr_evaluate_value(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic) {
    return expr_evaluate_public_value(program, root_object, is_active,
                                      active_user, NULL, out_value,
                                      diagnostic);
}

scxml_expr_status
scxml_expr_evaluate_value_with_system(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic) {
    return expr_evaluate_public_value(program, root_object, is_active,
                                      active_user, system_values, out_value,
                                      diagnostic);
}

void scxml_expr_program_destroy(
    scxml_expr_program *program) {
    scxml_expr_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (scxml_expr_program_impl *)program->impl;
    expr_program_impl_destroy(impl);
    program->impl = NULL;
}
