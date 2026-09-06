#include "voicexml_cmeta_expr.h"
#include "cmeta_location.h"
#include "voicexml_allocator.h"

#include <query_vm.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

typedef enum expr_operand_kind {
    EXPR_OPERAND_LOCATION = 1,
    EXPR_OPERAND_SINT,
    EXPR_OPERAND_UINT,
    EXPR_OPERAND_FLOAT,
    EXPR_OPERAND_STRING
} expr_operand_kind;

typedef enum expr_candidate_kind {
    EXPR_CANDIDATE_SCOPE = 1,
    EXPR_CANDIDATE_ROOT
} expr_candidate_kind;

typedef struct expr_token {
    expr_token_kind kind;
    size_t offset;
    size_t size;
} expr_token;

typedef struct expr_candidate {
    expr_candidate_kind kind;
    size_t scope_id;
    size_t slot;
    size_t root_field;
    size_t offset;
    const cmeta_scope_schema *schema;
    const cmeta_data_desc *data;
} expr_candidate;

typedef struct expr_operand {
    expr_operand_kind kind;
    vxml_cmeta_value_kind value_kind;
    const cmeta_data_desc *data;
    size_t candidate_first;
    size_t candidate_count;
    union {
        int64_t sint;
        uint64_t uint_value;
        double number;
        struct { const char *data; size_t size; } string;
    } value;
} expr_operand;

typedef struct vxml_cmeta_expr_program_impl {
    const cmeta_data_desc *root;
    char *source;
    qvm_instruction_t *instructions;
    expr_operand *operands;
    expr_candidate *candidates;
    uint32_t instruction_count;
    uint32_t operand_count;
    uint32_t register_count;
    size_t candidate_count;
    size_t root_field_count;
    size_t max_string_bytes;
    size_t scratch_bytes;
    vxml_cmeta_value_kind result_kind;
    qvm_limits_t qvm_limits;
    bool condition;
    bool direct_location;
} vxml_cmeta_expr_program_impl;

typedef struct expr_node {
    uint16_t reg;
    vxml_cmeta_value_kind kind;
    bool direct_location;
} expr_node;

typedef struct expr_parser {
    const char *source;
    size_t source_size;
    size_t cursor;
    expr_token token;
    const cmeta_data_desc *root;
    const vxml_cmeta_expr_compile_scope *scopes;
    size_t scope_count;
    vxml_cmeta_expr_limits limits;
    vxml_cmeta_expr_diagnostic *diagnostic;
    qvm_instruction_t *instructions;
    expr_operand *operands;
    expr_candidate *candidates;
    size_t instruction_capacity;
    size_t operand_capacity;
    size_t candidate_capacity;
    size_t instruction_count;
    size_t operand_count;
    size_t candidate_count;
    size_t literal_bytes;
    size_t expression_depth;
    size_t max_register;
    bool emit;
    vxml_status status;
} expr_parser;

typedef struct expr_eval_context {
    const vxml_cmeta_expr_program_impl *program;
    const vxml_cmeta_expr_runtime *runtime;
    vxml_cmeta_expr_scratch *scratch;
    vxml_status failure_status;
    bool failed;
} expr_eval_context;

static vxml_status expr_report(
    vxml_cmeta_expr_diagnostic *diagnostic, vxml_status status,
    size_t byte_offset, const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->byte_offset = byte_offset;
        if (message != NULL) {
            size_t size = strlen(message);
            if (size >= sizeof(diagnostic->message))
                size = sizeof(diagnostic->message) - 1u;
            memcpy(diagnostic->message, message, size);
        }
    }
    return status;
}

static bool parser_fail(expr_parser *parser, vxml_status status,
                        size_t offset, const char *message) {
    if (parser->status == VXML_OK) {
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
        bool decimal = false;
        bool exponent = false;
        while (parser->cursor < parser->source_size) {
            const char next = parser->source[parser->cursor];
            if (next >= '0' && next <= '9') {
                ++parser->cursor;
            } else if (next == '.' && !decimal && !exponent) {
                decimal = true;
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
                } else {
                    parser->token.kind = EXPR_TOKEN_NOT;
                }
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
                } else {
                    parser->token.kind = EXPR_TOKEN_LT;
                }
                break;
            case '>':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_GE;
                } else {
                    parser->token.kind = EXPR_TOKEN_GT;
                }
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
    if (parser->literal_bytes > parser->limits.max_literal_bytes ||
        count > parser->limits.max_literal_bytes - parser->literal_bytes)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression literal byte limit exceeded");
    parser->literal_bytes += count;
    return true;
}

static bool parser_emit_instruction(expr_parser *parser, qvm_opcode_t op,
                                    uint16_t dst, uint32_t arg,
                                    uint32_t src1, uint32_t src2) {
    if (parser->instruction_count >= parser->limits.max_instructions)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression instruction limit exceeded");
    if (parser->emit) {
        if (parser->instruction_count >= parser->instruction_capacity)
            return parser_fail(parser, VXML_INVALID_CONTRACT,
                               parser->token.offset,
                               "CMeta expression instruction count changed");
        parser->instructions[parser->instruction_count] =
            (qvm_instruction_t){(uint8_t)op, 0u, dst, arg, src1, src2};
    }
    ++parser->instruction_count;
    if ((size_t)dst + 1u > parser->max_register)
        parser->max_register = (size_t)dst + 1u;
    return true;
}

static bool parser_add_operand(expr_parser *parser, expr_operand operand,
                               uint32_t *out_index) {
    if (parser->operand_count >= parser->limits.max_operands ||
        parser->operand_count > UINT32_MAX)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression operand limit exceeded");
    *out_index = (uint32_t)parser->operand_count;
    if (parser->emit) {
        if (parser->operand_count >= parser->operand_capacity)
            return parser_fail(parser, VXML_INVALID_CONTRACT,
                               parser->token.offset,
                               "CMeta expression operand count changed");
        parser->operands[parser->operand_count] = operand;
    }
    ++parser->operand_count;
    return true;
}

static bool scalar_kind(const cmeta_data_desc *data,
                        vxml_cmeta_value_kind *out_kind) {
    size_t expected;
    if (!cmeta_data_desc_valid(data) || data->storage_type == NULL ||
        !cmeta_type_desc_valid(data->storage_type))
        return false;
    switch (data->kind) {
        case CMETA_DATA_BOOL:
            if (data->storage_type->size != sizeof(bool)) return false;
            *out_kind = VXML_CMETA_VALUE_BOOL;
            return true;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT: {
            const cmeta_data_integer_shape *shape =
                (const cmeta_data_integer_shape *)data->shape;
            if (shape == NULL ||
                (shape->bits != 8u && shape->bits != 16u &&
                 shape->bits != 32u && shape->bits != 64u))
                return false;
            expected = (size_t)shape->bits / CHAR_BIT;
            if (data->storage_type->size != expected) return false;
            *out_kind = data->kind == CMETA_DATA_SINT
                ? VXML_CMETA_VALUE_SINT : VXML_CMETA_VALUE_UINT;
            return true;
        }
        case CMETA_DATA_FLOAT: {
            const cmeta_data_float_shape *shape =
                (const cmeta_data_float_shape *)data->shape;
            if (shape == NULL || (shape->bits != 32u && shape->bits != 64u))
                return false;
            expected = (size_t)shape->bits / CHAR_BIT;
            if (data->storage_type->size != expected) return false;
            *out_kind = VXML_CMETA_VALUE_FLOAT;
            return true;
        }
        case CMETA_DATA_STRING: {
            const cmeta_data_buffer_ops *ops = cmeta_data_buffer_ops_of(data);
            if (ops == NULL ||
                ops->struct_size < offsetof(cmeta_data_buffer_ops, read) +
                    sizeof(ops->read) ||
                ops->read == NULL)
                return false;
            *out_kind = VXML_CMETA_VALUE_STRING;
            return true;
        }
        default:
            return false;
    }
}

static bool scalar_descriptors_compatible(const cmeta_data_desc *left,
                                          const cmeta_data_desc *right) {
    vxml_cmeta_value_kind left_kind;
    vxml_cmeta_value_kind right_kind;
    if (!scalar_kind(left, &left_kind) || !scalar_kind(right, &right_kind) ||
        left_kind != right_kind || left->kind != right->kind ||
        !cmeta_type_equal(left->storage_type, right->storage_type))
        return false;
    if (left->kind == CMETA_DATA_SINT || left->kind == CMETA_DATA_UINT)
        return ((const cmeta_data_integer_shape *)left->shape)->bits ==
               ((const cmeta_data_integer_shape *)right->shape)->bits;
    if (left->kind == CMETA_DATA_FLOAT)
        return ((const cmeta_data_float_shape *)left->shape)->bits ==
               ((const cmeta_data_float_shape *)right->shape)->bits;
    if (left->kind == CMETA_DATA_STRING) {
        const cmeta_data_buffer_shape *left_shape =
            (const cmeta_data_buffer_shape *)left->shape;
        const cmeta_data_buffer_shape *right_shape =
            (const cmeta_data_buffer_shape *)right->shape;
        const cmeta_data_buffer_ops *left_ops =
            cmeta_data_buffer_ops_of(left);
        const cmeta_data_buffer_ops *right_ops =
            cmeta_data_buffer_ops_of(right);
        return left_shape != NULL && right_shape != NULL &&
            left_ops != NULL && right_ops != NULL &&
            left_shape->ownership == right_shape->ownership &&
            left_ops->ownership == right_ops->ownership &&
            left_ops->abi_version == right_ops->abi_version &&
            cmeta_type_equal(left_ops->storage_type,
                             right_ops->storage_type) &&
            left_ops->storage_type->kind == right_ops->storage_type->kind &&
            left_ops->storage_type->size == right_ops->storage_type->size &&
            left_ops->storage_type->align == right_ops->storage_type->align;
    }
    return true;
}

static bool parser_add_candidate(expr_parser *parser,
                                 expr_candidate candidate,
                                 const cmeta_data_desc **common_data,
                                 vxml_cmeta_value_kind *common_kind) {
    vxml_cmeta_value_kind kind;
    if (!scalar_kind(candidate.data, &kind))
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "CMeta location terminal is not a supported scalar");
    if (*common_data == NULL) {
        *common_data = candidate.data;
        *common_kind = kind;
    } else if (!scalar_descriptors_compatible(*common_data, candidate.data)) {
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "CMeta location candidates have incompatible types");
    }
    if (parser->candidate_count == SIZE_MAX)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression candidate count overflow");
    if (parser->emit) {
        if (parser->candidate_count >= parser->candidate_capacity)
            return parser_fail(parser, VXML_INVALID_CONTRACT,
                               parser->token.offset,
                               "CMeta expression candidate count changed");
        parser->candidates[parser->candidate_count] = candidate;
    }
    ++parser->candidate_count;
    return true;
}

static bool compile_schema_valid(const cmeta_scope_schema *schema) {
    size_t index;
    if (schema == NULL || schema->slot_count > schema->slot_capacity ||
        schema->storage_size > schema->max_storage_bytes ||
        (schema->slot_count != 0u && schema->slots == NULL))
        return false;
    for (index = 0u; index < schema->slot_count; ++index) {
        const cmeta_scope_slot *slot = &schema->slots[index];
        if (slot->name == NULL || slot->name_size == 0u ||
            !cmeta_data_desc_valid(slot->value) ||
            slot->value->storage_type == NULL ||
            slot->offset > schema->storage_size ||
            slot->value->storage_type->size >
                schema->storage_size - slot->offset)
            return false;
    }
    return true;
}

static const cmeta_data_field_desc *find_root_field(
    const cmeta_data_desc *root, const char *name, size_t name_size,
    size_t *out_index) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)root->shape;
    size_t index;
    *out_index = SIZE_MAX;
    if (shape == NULL || (shape->field_count != 0u && shape->fields == NULL))
        return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0) {
            *out_index = index;
            return field;
        }
    }
    return NULL;
}

static bool parser_location_error(expr_parser *parser,
                                  cmeta_location_status status,
                                  const cmeta_location_diagnostic *diagnostic,
                                  size_t path_offset) {
    if (status == CMETA_LOCATION_LIMIT_EXCEEDED)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           path_offset + diagnostic->byte_offset,
                           "CMeta location path depth limit exceeded");
    return parser_fail(parser, VXML_SEMANTIC_ERROR,
                       path_offset + diagnostic->byte_offset,
                       "CMeta location path is unknown or invalid");
}

static bool parser_compile_location(expr_parser *parser, uint16_t target,
                                    expr_node *out, size_t path_begin,
                                    size_t path_size, size_t first_size) {
    const char *path = parser->source + path_begin;
    const cmeta_data_desc *common_data = NULL;
    vxml_cmeta_value_kind common_kind = VXML_CMETA_VALUE_UNDEFINED;
    expr_operand operand = {0};
    size_t candidate_begin = parser->candidate_count;
    size_t index;
    uint32_t operand_index;
    for (index = 0u; index < parser->scope_count; ++index) {
        const vxml_cmeta_expr_compile_scope *compile_scope =
            &parser->scopes[index];
        const cmeta_scope_slot *slot;
        size_t slot_index = SIZE_MAX;
        expr_candidate candidate = {0};
        slot = cmeta_scope_find(compile_scope->schema, path, first_size,
                                &slot_index);
        if (slot == NULL) continue;
        candidate.kind = EXPR_CANDIDATE_SCOPE;
        candidate.scope_id = compile_scope->scope_id;
        candidate.slot = slot_index;
        candidate.root_field = SIZE_MAX;
        candidate.schema = compile_scope->schema;
        candidate.data = slot->value;
        if (path_size > first_size) {
            cmeta_location location = {0};
            cmeta_location_diagnostic diagnostic = {0};
            const char *suffix = path + first_size + 1u;
            const size_t suffix_size = path_size - first_size - 1u;
            const cmeta_location_status status =
                cmeta_location_compile_detailed(
                    &location, suffix, suffix_size, slot->value,
                    parser->limits.max_path_depth - 1u, &diagnostic);
            if (status != CMETA_LOCATION_OK)
                return parser_location_error(
                    parser, status, &diagnostic,
                    path_begin + first_size + 1u);
            candidate.data = location.value;
            candidate.offset = location.offset;
        }
        if (!parser_add_candidate(parser, candidate,
                                  &common_data, &common_kind))
            return false;
    }
    {
        size_t root_field_index;
        const cmeta_data_field_desc *root_field = find_root_field(
            parser->root, path, first_size, &root_field_index);
        if (root_field != NULL) {
            cmeta_location location = {0};
            cmeta_location_diagnostic diagnostic = {0};
            const cmeta_location_status status =
                cmeta_location_compile_detailed(
                    &location, path, path_size, parser->root,
                    parser->limits.max_path_depth, &diagnostic);
            expr_candidate candidate = {0};
            if (status != CMETA_LOCATION_OK)
                return parser_location_error(
                    parser, status, &diagnostic, path_begin);
            candidate.kind = EXPR_CANDIDATE_ROOT;
            candidate.scope_id = SIZE_MAX;
            candidate.slot = SIZE_MAX;
            candidate.root_field = root_field_index;
            candidate.offset = location.offset;
            candidate.data = location.value;
            if (!parser_add_candidate(parser, candidate,
                                      &common_data, &common_kind))
                return false;
        }
    }
    if (parser->candidate_count == candidate_begin)
        return parser_fail(parser, VXML_SEMANTIC_ERROR, path_begin,
                           "CMeta expression names an unknown location");
    operand.kind = EXPR_OPERAND_LOCATION;
    operand.value_kind = common_kind;
    operand.data = common_data;
    operand.candidate_first = candidate_begin;
    operand.candidate_count = parser->candidate_count - candidate_begin;
    if (!parser_add_operand(parser, operand, &operand_index) ||
        !parser_emit_instruction(parser, QVM_OP_LOAD_PATH, target, 0u,
                                 operand_index, 0u))
        return false;
    out->reg = target;
    out->kind = common_kind;
    out->direct_location = true;
    return true;
}

static bool parser_parse_location(expr_parser *parser, uint16_t target,
                                  expr_node *out) {
    const size_t path_begin = parser->token.offset;
    const size_t first_size = parser->token.size;
    size_t path_end = path_begin + first_size;
    size_t depth = 1u;
    parser_next(parser);
    while (parser->token.kind == EXPR_TOKEN_DOT) {
        const size_t dot_end = parser->token.offset + parser->token.size;
        if (parser->token.offset != path_end)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "CMeta location cannot contain whitespace");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT ||
            parser->token.offset != dot_end)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "CMeta location requires a field after '.'");
        if (++depth > parser->limits.max_path_depth)
            return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "CMeta location path depth limit exceeded");
        path_end = parser->token.offset + parser->token.size;
        parser_next(parser);
    }
    return parser_compile_location(parser, target, out, path_begin,
                                   path_end - path_begin, first_size);
}

static bool value_is_numeric(vxml_cmeta_value_kind kind) {
    return kind == VXML_CMETA_VALUE_SINT ||
           kind == VXML_CMETA_VALUE_UINT ||
           kind == VXML_CMETA_VALUE_FLOAT;
}

static bool parse_number_operand(expr_parser *parser, expr_operand *operand) {
    char *text;
    const char *begin = parser->source + parser->token.offset;
    size_t size = parser->token.size;
    bool unsigned_suffix = false;
    bool floating = false;
    bool invalid;
    char *end = NULL;
    size_t index;
    if (!parser_add_literal_bytes(parser, size)) return false;
    if (size == 0u || size == SIZE_MAX)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta numeric literal is too long");
    text = (char *)vxml_malloc(size + 1u);
    if (text == NULL)
        return parser_fail(parser, VXML_ALLOCATION_FAILED,
                           parser->token.offset,
                           "CMeta numeric literal allocation failed");
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
        if (unsigned_suffix) {
            vxml_free(text);
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "floating literal cannot be unsigned");
        }
        operand->kind = EXPR_OPERAND_FLOAT;
        operand->value_kind = VXML_CMETA_VALUE_FLOAT;
        operand->value.number = strtod(text, &end);
        if (!isfinite(operand->value.number)) errno = ERANGE;
    } else if (unsigned_suffix) {
        operand->kind = EXPR_OPERAND_UINT;
        operand->value_kind = VXML_CMETA_VALUE_UINT;
        operand->value.uint_value = strtoull(text, &end, 10);
    } else {
        operand->kind = EXPR_OPERAND_SINT;
        operand->value_kind = VXML_CMETA_VALUE_SINT;
        operand->value.sint = strtoll(text, &end, 10);
    }
    invalid = errno == ERANGE || end == text || *end != '\0';
    vxml_free(text);
    if (invalid)
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "numeric literal is invalid or out of range");
    return true;
}

static bool parser_parse_or(expr_parser *parser, uint16_t target,
                            expr_node *out);

static bool parser_parse_primary(expr_parser *parser, uint16_t target,
                                 expr_node *out) {
    if (target >= QVM_MAX_REGISTERS)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression register limit exceeded");
    if (parser->token.kind == EXPR_TOKEN_LPAREN) {
        parser_next(parser);
        if (!parser_parse_or(parser, target, out)) return false;
        if (parser->token.kind != EXPR_TOKEN_RPAREN)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "CMeta expression requires ')'");
        parser_next(parser);
        return true;
    }
    if (parser->token.kind == EXPR_TOKEN_NUMBER) {
        expr_operand operand = {0};
        uint32_t operand_index;
        if (!parse_number_operand(parser, &operand)) return false;
        out->reg = target;
        out->kind = operand.value_kind;
        out->direct_location = false;
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
        if (!parser_add_literal_bytes(parser, parser->token.size))
            return false;
        if (parser->token.size > parser->limits.max_string_bytes)
            return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "CMeta string literal byte limit exceeded");
        operand.kind = EXPR_OPERAND_STRING;
        operand.value_kind = VXML_CMETA_VALUE_STRING;
        operand.value.string.size = parser->token.size;
        if (parser->emit)
            operand.value.string.data =
                parser->source + parser->token.offset;
        out->reg = target;
        out->kind = VXML_CMETA_VALUE_STRING;
        out->direct_location = false;
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        parser_next(parser);
        return true;
    }
    if (parser->token.kind != EXPR_TOKEN_IDENT)
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "CMeta expression requires a scalar value");
    if (token_text_equal(parser, "true") ||
        token_text_equal(parser, "false")) {
        const bool value = token_text_equal(parser, "true");
        out->reg = target;
        out->kind = VXML_CMETA_VALUE_BOOL;
        out->direct_location = false;
        parser_next(parser);
        return parser_emit_instruction(
            parser, value ? QVM_OP_TRUE : QVM_OP_FALSE,
            target, 0u, 0u, 0u);
    }
    if (token_text_equal(parser, "_event") ||
        token_text_equal(parser, "_name") ||
        token_text_equal(parser, "_sessionid") ||
        token_text_equal(parser, "_ioprocessors") ||
        token_text_equal(parser, "In") ||
        token_text_equal(parser, "is_bound") ||
        token_text_equal(parser, "isBound"))
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "name is unavailable in the VoiceXML CMeta profile");
    return parser_parse_location(parser, target, out);
}

static bool parser_parse_unary(expr_parser *parser, uint16_t target,
                               expr_node *out) {
    expr_token_kind operation = EXPR_TOKEN_END;
    bool ok;
    if (target >= QVM_MAX_REGISTERS)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression register limit exceeded");
    if (parser->expression_depth >= parser->limits.max_expression_depth)
        return parser_fail(parser, VXML_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression depth limit exceeded");
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
            expr_operand operand = {0};
            uint32_t operand_index;
            operand.kind = EXPR_OPERAND_SINT;
            operand.value_kind = VXML_CMETA_VALUE_SINT;
            operand.value.sint = INT64_MIN;
            ok = parser_add_literal_bytes(parser, parser->token.size) &&
                 parser_add_operand(parser, operand, &operand_index) &&
                 parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target,
                                         0u, operand_index, 0u);
            if (ok) {
                out->reg = target;
                out->kind = VXML_CMETA_VALUE_SINT;
                out->direct_location = false;
                parser_next(parser);
            }
            --parser->expression_depth;
            return ok;
        }
        ok = parser_parse_unary(parser, target, out);
    } else {
        ok = parser_parse_primary(parser, target, out);
    }
    --parser->expression_depth;
    if (!ok) return false;
    if (operation == EXPR_TOKEN_NOT) {
        if (out->kind != VXML_CMETA_VALUE_BOOL)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "logical not requires a Boolean operand");
        out->direct_location = false;
        return parser_emit_instruction(parser, QVM_OP_NOT, target, 0u,
                                       target, 0u);
    }
    if (operation == EXPR_TOKEN_PLUS) {
        if (!value_is_numeric(out->kind))
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "unary plus requires a numeric operand");
        out->direct_location = false;
        return true;
    }
    if (operation == EXPR_TOKEN_MINUS) {
        if (out->kind != VXML_CMETA_VALUE_SINT &&
            out->kind != VXML_CMETA_VALUE_FLOAT)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "unary minus requires signed or floating data");
        out->direct_location = false;
        return parser_emit_instruction(parser, QVM_OP_NEG, target, 0u,
                                       target, 0u);
    }
    return true;
}

static bool parser_arithmetic_kind(expr_parser *parser,
                                   expr_token_kind operation,
                                   vxml_cmeta_value_kind left,
                                   vxml_cmeta_value_kind right,
                                   vxml_cmeta_value_kind *out_kind) {
    if (!value_is_numeric(left) || !value_is_numeric(right))
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "arithmetic requires numeric operands");
    if (operation == EXPR_TOKEN_PERCENT) {
        if (left != right ||
            (left != VXML_CMETA_VALUE_SINT &&
             left != VXML_CMETA_VALUE_UINT))
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "remainder requires matching integral operands");
        *out_kind = left;
        return true;
    }
    if (left == VXML_CMETA_VALUE_FLOAT || right == VXML_CMETA_VALUE_FLOAT) {
        *out_kind = VXML_CMETA_VALUE_FLOAT;
        return true;
    }
    if (left != right)
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "signed and unsigned arithmetic cannot be mixed");
    *out_kind = left;
    return true;
}

static bool parser_parse_multiplicative(expr_parser *parser,
                                        uint16_t target, expr_node *out) {
    if (!parser_parse_unary(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_STAR ||
           parser->token.kind == EXPR_TOKEN_SLASH ||
           parser->token.kind == EXPR_TOKEN_PERCENT) {
        const expr_token_kind operation = parser->token.kind;
        const qvm_opcode_t opcode = operation == EXPR_TOKEN_STAR
            ? QVM_OP_MUL : operation == EXPR_TOKEN_SLASH
            ? QVM_OP_DIV : QVM_OP_MOD;
        expr_node right;
        vxml_cmeta_value_kind result_kind;
        parser_next(parser);
        if (!parser_parse_unary(parser, (uint16_t)(target + 1u), &right) ||
            !parser_arithmetic_kind(parser, operation, out->kind,
                                    right.kind, &result_kind) ||
            !parser_emit_instruction(parser, opcode, target, 0u,
                                     target, right.reg))
            return false;
        out->kind = result_kind;
        out->direct_location = false;
    }
    return true;
}

static bool parser_parse_additive(expr_parser *parser, uint16_t target,
                                  expr_node *out) {
    if (!parser_parse_multiplicative(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_PLUS ||
           parser->token.kind == EXPR_TOKEN_MINUS) {
        const expr_token_kind operation = parser->token.kind;
        const qvm_opcode_t opcode = operation == EXPR_TOKEN_PLUS
            ? QVM_OP_ADD : QVM_OP_SUB;
        expr_node right;
        vxml_cmeta_value_kind result_kind;
        parser_next(parser);
        if (!parser_parse_multiplicative(
                parser, (uint16_t)(target + 1u), &right) ||
            !parser_arithmetic_kind(parser, operation, out->kind,
                                    right.kind, &result_kind) ||
            !parser_emit_instruction(parser, opcode, target, 0u,
                                     target, right.reg))
            return false;
        out->kind = result_kind;
        out->direct_location = false;
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
        if (!((out->kind == VXML_CMETA_VALUE_BOOL &&
               right.kind == VXML_CMETA_VALUE_BOOL) ||
              (out->kind == VXML_CMETA_VALUE_STRING &&
               right.kind == VXML_CMETA_VALUE_STRING) ||
              (value_is_numeric(out->kind) && value_is_numeric(right.kind))))
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "equality operands have incompatible types");
    } else if (!((out->kind == VXML_CMETA_VALUE_STRING &&
                  right.kind == VXML_CMETA_VALUE_STRING) ||
                 (value_is_numeric(out->kind) &&
                  value_is_numeric(right.kind)))) {
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "ordered comparison requires numeric or string data");
    }
    comparison = operation == EXPR_TOKEN_EQ ? 0u :
                 operation == EXPR_TOKEN_NE ? 1u :
                 operation == EXPR_TOKEN_LT ? 2u :
                 operation == EXPR_TOKEN_LE ? 3u :
                 operation == EXPR_TOKEN_GT ? 4u : 5u;
    if (!parser_emit_instruction(parser, QVM_OP_CMP, target, comparison,
                                 target, right.reg))
        return false;
    out->kind = VXML_CMETA_VALUE_BOOL;
    out->direct_location = false;
    return true;
}

static bool parser_parse_and(expr_parser *parser, uint16_t target,
                             expr_node *out) {
    if (!parser_parse_compare(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_AND) {
        expr_node right;
        const size_t jump_index = parser->instruction_count;
        if (out->kind != VXML_CMETA_VALUE_BOOL)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "logical and requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_JMP_FALSE, 0u, 0u,
                                     target, 0u))
            return false;
        parser_next(parser);
        if (!parser_parse_compare(parser, (uint16_t)(target + 1u), &right))
            return false;
        if (right.kind != VXML_CMETA_VALUE_BOOL)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "logical and requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_BAND, target, 0u,
                                     target, right.reg))
            return false;
        if (parser->emit)
            parser->instructions[jump_index].arg =
                (uint32_t)parser->instruction_count;
        out->kind = VXML_CMETA_VALUE_BOOL;
        out->direct_location = false;
    }
    return true;
}

static bool parser_parse_or(expr_parser *parser, uint16_t target,
                            expr_node *out) {
    if (!parser_parse_and(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_OR) {
        expr_node right;
        const size_t jump_index = parser->instruction_count;
        if (out->kind != VXML_CMETA_VALUE_BOOL)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "logical or requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_JMP_TRUE, 0u, 0u,
                                     target, 0u))
            return false;
        parser_next(parser);
        if (!parser_parse_and(parser, (uint16_t)(target + 1u), &right))
            return false;
        if (right.kind != VXML_CMETA_VALUE_BOOL)
            return parser_fail(parser, VXML_SEMANTIC_ERROR,
                               parser->token.offset,
                               "logical or requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_BOR, target, 0u,
                                     target, right.reg))
            return false;
        if (parser->emit)
            parser->instructions[jump_index].arg =
                (uint32_t)parser->instruction_count;
        out->kind = VXML_CMETA_VALUE_BOOL;
        out->direct_location = false;
    }
    return true;
}

static bool parser_run(expr_parser *parser, bool require_boolean,
                       expr_node *out_root) {
    expr_node root;
    parser_next(parser);
    if (parser->token.kind == EXPR_TOKEN_INVALID)
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "CMeta expression contains an invalid token");
    if (!parser_parse_or(parser, 0u, &root)) return false;
    if (parser->token.kind != EXPR_TOKEN_END)
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->token.offset,
                           "CMeta expression has trailing input");
    if (require_boolean && root.kind != VXML_CMETA_VALUE_BOOL)
        return parser_fail(parser, VXML_SEMANTIC_ERROR,
                           parser->source_size,
                           "CMeta condition result must be Boolean");
    *out_root = root;
    return true;
}

bool vxml_cmeta_expr_limits_valid(const vxml_cmeta_expr_limits *limits) {
    return limits != NULL && limits->max_source_bytes != 0u &&
        limits->max_instructions != 0u &&
        limits->max_instructions <= UINT32_MAX &&
        limits->max_operands != 0u && limits->max_operands <= UINT32_MAX &&
        limits->max_expression_depth != 0u &&
        limits->max_expression_depth <= QVM_MAX_REGISTERS &&
        limits->max_path_depth != 0u && limits->max_literal_bytes != 0u &&
        limits->max_string_bytes != 0u;
}

static void expr_program_impl_destroy(vxml_cmeta_expr_program_impl *impl) {
    if (impl == NULL) return;
    vxml_free(impl->candidates);
    vxml_free(impl->operands);
    vxml_free(impl->instructions);
    vxml_free(impl->source);
    vxml_free(impl);
}

static bool compile_inputs_valid(
    const cmeta_data_desc *root,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count) {
    const cmeta_data_struct_shape *shape;
    size_t index;
    size_t other;
    if (!cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL || root->shape == NULL ||
        (scope_count != 0u && scopes == NULL))
        return false;
    shape = (const cmeta_data_struct_shape *)root->shape;
    if (shape->field_count != 0u && shape->fields == NULL) return false;
    for (index = 0u; index < scope_count; ++index) {
        if (scopes[index].scope_id == SIZE_MAX ||
            !compile_schema_valid(scopes[index].schema))
            return false;
        for (other = 0u; other < index; ++other)
            if (scopes[other].scope_id == scopes[index].scope_id)
                return false;
    }
    return true;
}

static bool expr_program_measure_scratch(
    const vxml_cmeta_expr_program_impl *impl, size_t *out_bytes) {
    size_t bytes = 0u;
    size_t index;
    for (index = 0u; index < impl->operand_count; ++index) {
        const expr_operand *operand = &impl->operands[index];
        if (operand->kind != EXPR_OPERAND_LOCATION ||
            operand->value_kind != VXML_CMETA_VALUE_STRING)
            continue;
        if (bytes > SIZE_MAX - impl->max_string_bytes) return false;
        bytes += impl->max_string_bytes;
    }
    *out_bytes = bytes;
    return true;
}

static vxml_status expr_compile(
    vxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic,
    bool require_boolean) {
    expr_parser parser;
    expr_node admitted;
    expr_node emitted;
    vxml_cmeta_expr_program_impl *impl = NULL;
    const cmeta_data_struct_shape *root_shape;
    qvm_diagnostic_t qvm_diagnostic = {0};
    size_t instruction_count;
    size_t operand_count;
    size_t candidate_count;
    int qvm_status;
    if (out != NULL) out->impl = NULL;
    if (out == NULL || source == NULL || source_size == 0u ||
        !vxml_cmeta_expr_limits_valid(limits) ||
        !compile_inputs_valid(root, scopes, scope_count))
        return expr_report(diagnostic, VXML_INVALID_ARGUMENT, 0u,
                           "invalid VoiceXML CMeta expression arguments");
    if (source_size > limits->max_source_bytes)
        return expr_report(diagnostic, VXML_LIMIT_EXCEEDED,
                           limits->max_source_bytes,
                           "VoiceXML CMeta expression source limit exceeded");
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.source_size = source_size;
    parser.root = root;
    parser.scopes = scopes;
    parser.scope_count = scope_count;
    parser.limits = *limits;
    parser.diagnostic = diagnostic;
    parser.status = VXML_OK;
    if (!parser_run(&parser, require_boolean, &admitted))
        return parser.status;
    instruction_count = parser.instruction_count;
    operand_count = parser.operand_count;
    candidate_count = parser.candidate_count;
    if (parser.instruction_count > SIZE_MAX / sizeof(qvm_instruction_t) ||
        parser.operand_count > SIZE_MAX / sizeof(expr_operand) ||
        parser.candidate_count > SIZE_MAX / sizeof(expr_candidate))
        return expr_report(diagnostic, VXML_LIMIT_EXCEEDED, 0u,
                           "CMeta expression program size overflow");

    impl = (vxml_cmeta_expr_program_impl *)vxml_calloc(1u, sizeof(*impl));
    if (impl != NULL) impl->source = (char *)vxml_malloc(source_size);
    if (impl != NULL && impl->source != NULL)
        impl->instructions = (qvm_instruction_t *)vxml_calloc(
            instruction_count, sizeof(*impl->instructions));
    if (impl != NULL && impl->instructions != NULL &&
        operand_count != 0u)
        impl->operands = (expr_operand *)vxml_calloc(
            operand_count, sizeof(*impl->operands));
    if (impl != NULL && impl->instructions != NULL &&
        (operand_count == 0u || impl->operands != NULL) &&
        candidate_count != 0u)
        impl->candidates = (expr_candidate *)vxml_calloc(
            candidate_count, sizeof(*impl->candidates));
    if (impl == NULL || impl->source == NULL || impl->instructions == NULL ||
        (operand_count != 0u && impl->operands == NULL) ||
        (candidate_count != 0u && impl->candidates == NULL)) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic, VXML_ALLOCATION_FAILED, 0u,
                           "VoiceXML CMeta expression allocation failed");
    }
    memcpy(impl->source, source, source_size);
    memset(&parser, 0, sizeof(parser));
    parser.source = impl->source;
    parser.source_size = source_size;
    parser.root = root;
    parser.scopes = scopes;
    parser.scope_count = scope_count;
    parser.limits = *limits;
    parser.diagnostic = diagnostic;
    parser.instructions = impl->instructions;
    parser.operands = impl->operands;
    parser.candidates = impl->candidates;
    parser.instruction_capacity = instruction_count;
    parser.operand_capacity = operand_count;
    parser.candidate_capacity = candidate_count;
    parser.emit = true;
    parser.status = VXML_OK;
    if (!parser_run(&parser, require_boolean, &emitted) ||
        parser.instruction_count != parser.instruction_capacity ||
        parser.operand_count != parser.operand_capacity ||
        parser.candidate_count != parser.candidate_capacity ||
        emitted.kind != admitted.kind) {
        const vxml_status failure = parser.status != VXML_OK
            ? parser.status : VXML_INVALID_CONTRACT;
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic, failure, 0u,
                           "CMeta expression emission changed after admission");
    }
    root_shape = (const cmeta_data_struct_shape *)root->shape;
    impl->root = root;
    impl->instruction_count = (uint32_t)parser.instruction_count;
    impl->operand_count = (uint32_t)parser.operand_count;
    impl->register_count = (uint32_t)parser.max_register;
    impl->candidate_count = parser.candidate_count;
    impl->root_field_count = root_shape->field_count;
    impl->max_string_bytes = limits->max_string_bytes;
    if (!expr_program_measure_scratch(impl, &impl->scratch_bytes)) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic, VXML_LIMIT_EXCEEDED, 0u,
                           "CMeta expression scratch size overflow");
    }
    impl->result_kind = emitted.kind;
    impl->condition = require_boolean;
    impl->direct_location = emitted.direct_location;
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
        return expr_report(diagnostic, VXML_INVALID_CONTRACT,
                           qvm_diagnostic.instruction,
                           qvm_diagnostic.message);
    }
    out->impl = impl;
    return expr_report(diagnostic, VXML_OK, 0u, NULL);
}

vxml_status vxml_cmeta_expr_compile_condition(
    vxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, scopes, scope_count,
                        limits, diagnostic, true);
}

vxml_status vxml_cmeta_expr_compile_value(
    vxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, scopes, scope_count,
                        limits, diagnostic, false);
}

vxml_cmeta_value_kind vxml_cmeta_expr_program_value_kind(
    const vxml_cmeta_expr_program *program) {
    const vxml_cmeta_expr_program_impl *impl = program != NULL
        ? (const vxml_cmeta_expr_program_impl *)program->impl : NULL;
    return impl != NULL ? impl->result_kind : VXML_CMETA_VALUE_UNDEFINED;
}

size_t vxml_cmeta_expr_program_scratch_bytes(
    const vxml_cmeta_expr_program *program) {
    const vxml_cmeta_expr_program_impl *impl = program != NULL
        ? (const vxml_cmeta_expr_program_impl *)program->impl : NULL;
    return impl != NULL ? impl->scratch_bytes : 0u;
}

static void make_qvm_value(qvm_value_t *out,
                           vxml_cmeta_value_kind kind) {
    memset(out, 0, sizeof(*out));
    out->type = (int)kind;
}

static bool read_integer(const cmeta_data_desc *data, const void *object,
                         qvm_value_t *out) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)data->shape)->bits;
    if (data->kind == CMETA_DATA_SINT) {
        make_qvm_value(out, VXML_CMETA_VALUE_SINT);
        switch (bits) {
            case 8u: {
                int8_t value;
                memcpy(&value, object, sizeof(value));
                out->integer = value;
                return true;
            }
            case 16u: {
                int16_t value;
                memcpy(&value, object, sizeof(value));
                out->integer = value;
                return true;
            }
            case 32u: {
                int32_t value;
                memcpy(&value, object, sizeof(value));
                out->integer = value;
                return true;
            }
            case 64u:
                memcpy(&out->integer, object, sizeof(out->integer));
                return true;
        }
    } else if (data->kind == CMETA_DATA_UINT) {
        make_qvm_value(out, VXML_CMETA_VALUE_UINT);
        switch (bits) {
            case 8u: {
                uint8_t value;
                memcpy(&value, object, sizeof(value));
                out->uinteger = value;
                return true;
            }
            case 16u: {
                uint16_t value;
                memcpy(&value, object, sizeof(value));
                out->uinteger = value;
                return true;
            }
            case 32u: {
                uint32_t value;
                memcpy(&value, object, sizeof(value));
                out->uinteger = value;
                return true;
            }
            case 64u:
                memcpy(&out->uinteger, object, sizeof(out->uinteger));
                return true;
        }
    }
    return false;
}

static bool copy_runtime_string(expr_eval_context *context,
                                const unsigned char *bytes, size_t size,
                                qvm_value_t *out) {
    unsigned char *destination = NULL;
    if (context->scratch->used > context->scratch->capacity ||
        size > context->scratch->capacity - context->scratch->used) {
        context->failure_status = VXML_LIMIT_EXCEEDED;
        return false;
    }
    if (size != 0u && (bytes == NULL || context->scratch->bytes == NULL))
        return false;
    if (size != 0u) {
        destination = context->scratch->bytes + context->scratch->used;
        memmove(destination, bytes, size);
    }
    context->scratch->used += size;
    make_qvm_value(out, VXML_CMETA_VALUE_STRING);
    out->str = (const char *)destination;
    out->length = size;
    return true;
}

static bool read_scalar(expr_eval_context *context,
                        const cmeta_data_desc *data,
                        const void *object, qvm_value_t *out) {
    vxml_cmeta_value_kind kind;
    if (object == NULL || !scalar_kind(data, &kind)) return false;
    switch (data->kind) {
        case CMETA_DATA_BOOL: {
            bool value;
            memcpy(&value, object, sizeof(value));
            make_qvm_value(out, VXML_CMETA_VALUE_BOOL);
            out->boolean = value;
            return true;
        }
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            return read_integer(data, object, out);
        case CMETA_DATA_FLOAT: {
            const uint8_t bits =
                ((const cmeta_data_float_shape *)data->shape)->bits;
            make_qvm_value(out, VXML_CMETA_VALUE_FLOAT);
            if (bits == 32u) {
                float value;
                memcpy(&value, object, sizeof(value));
                out->number = value;
                return isfinite(out->number) != 0;
            }
            if (bits == 64u) {
                memcpy(&out->number, object, sizeof(out->number));
                return isfinite(out->number) != 0;
            }
            return false;
        }
        case CMETA_DATA_STRING: {
            const unsigned char *bytes = NULL;
            size_t size = 0u;
            if (cmeta_data_buffer_read(
                    data, object, context->program->max_string_bytes,
                    &bytes, &size) != CMETA_OK)
                return false;
            return copy_runtime_string(context, bytes, size, out);
        }
        default:
            return false;
    }
}

static const vxml_cmeta_expr_runtime_scope *find_runtime_scope(
    const vxml_cmeta_expr_runtime *runtime, size_t scope_id) {
    size_t index;
    const vxml_cmeta_expr_runtime_scope *found = NULL;
    for (index = 0u; index < runtime->scope_count; ++index) {
        const vxml_cmeta_expr_runtime_scope *candidate =
            &runtime->scopes[index];
        if (candidate->scope_id != scope_id) continue;
        if (found != NULL) return NULL;
        found = candidate;
    }
    return found;
}

static bool runtime_valid(const vxml_cmeta_expr_program_impl *program,
                          const vxml_cmeta_expr_runtime *runtime) {
    size_t index;
    size_t other;
    if (runtime == NULL || runtime->root == NULL ||
        runtime->root_bound_count != program->root_field_count ||
        (program->root_field_count != 0u && runtime->root_bound == NULL) ||
        (runtime->scope_count != 0u && runtime->scopes == NULL))
        return false;
    for (index = 0u; index < runtime->scope_count; ++index) {
        const vxml_cmeta_expr_runtime_scope *scope = &runtime->scopes[index];
        if (scope->scope_id == SIZE_MAX ||
            !cmeta_scope_view_valid(scope->view) ||
            scope->declared_count != scope->view->schema->slot_count ||
            (scope->declared_count != 0u && scope->declared == NULL))
            return false;
        for (other = 0u; other < index; ++other)
            if (runtime->scopes[other].scope_id == scope->scope_id)
                return false;
    }
    return true;
}

static int resolve_location(expr_eval_context *context,
                            const expr_operand *operand,
                            qvm_value_t *out) {
    size_t index;
    if (operand->candidate_first > context->program->candidate_count ||
        operand->candidate_count > context->program->candidate_count -
            operand->candidate_first) {
        context->failed = true;
        return 0;
    }
    for (index = 0u; index < operand->candidate_count; ++index) {
        const expr_candidate *candidate =
            &context->program->candidates[
                operand->candidate_first + index];
        const unsigned char *base;
        size_t base_size;
        bool bound;
        if (!scalar_descriptors_compatible(operand->data, candidate->data)) {
            context->failed = true;
            return 0;
        }
        if (candidate->kind == EXPR_CANDIDATE_SCOPE) {
            const vxml_cmeta_expr_runtime_scope *scope =
                find_runtime_scope(context->runtime, candidate->scope_id);
            const cmeta_scope_slot *slot;
            if (scope == NULL || scope->view->schema != candidate->schema ||
                candidate->slot >= scope->view->schema->slot_count ||
                candidate->slot >= scope->declared_count) {
                context->failed = true;
                return 0;
            }
            if (scope->declared[candidate->slot] == 0u) continue;
            bound = scope->view->bound[candidate->slot] != 0u;
            if (!bound) {
                make_qvm_value(out, VXML_CMETA_VALUE_UNDEFINED);
                return 1;
            }
            slot = &scope->view->schema->slots[candidate->slot];
            base = scope->view->storage + slot->offset;
            base_size = slot->value->storage_type->size;
        } else if (candidate->kind == EXPR_CANDIDATE_ROOT) {
            if (candidate->root_field >= context->runtime->root_bound_count) {
                context->failed = true;
                return 0;
            }
            if (context->runtime->root_bound[candidate->root_field] == 0u) {
                make_qvm_value(out, VXML_CMETA_VALUE_UNDEFINED);
                return 1;
            }
            base = (const unsigned char *)context->runtime->root;
            base_size = context->program->root->storage_type->size;
        } else {
            context->failed = true;
            return 0;
        }
        if (candidate->offset > base_size ||
            candidate->data->storage_type->size >
                base_size - candidate->offset ||
            !read_scalar(context, candidate->data,
                         base + candidate->offset, out)) {
            context->failed = true;
            return 0;
        }
        return 1;
    }
    context->failed = true;
    return 0;
}

static int expr_resolve(void *user, uint32_t index, qvm_value_t *out) {
    expr_eval_context *context = (expr_eval_context *)user;
    const expr_operand *operand;
    if (context == NULL || out == NULL ||
        index >= context->program->operand_count) {
        if (context != NULL) context->failed = true;
        return 0;
    }
    operand = &context->program->operands[index];
    switch (operand->kind) {
        case EXPR_OPERAND_LOCATION:
            return resolve_location(context, operand, out);
        case EXPR_OPERAND_SINT:
            make_qvm_value(out, VXML_CMETA_VALUE_SINT);
            out->integer = operand->value.sint;
            return 1;
        case EXPR_OPERAND_UINT:
            make_qvm_value(out, VXML_CMETA_VALUE_UINT);
            out->uinteger = operand->value.uint_value;
            return 1;
        case EXPR_OPERAND_FLOAT:
            make_qvm_value(out, VXML_CMETA_VALUE_FLOAT);
            out->number = operand->value.number;
            return 1;
        case EXPR_OPERAND_STRING:
            make_qvm_value(out, VXML_CMETA_VALUE_STRING);
            out->str = operand->value.string.data;
            out->length = operand->value.string.size;
            return 1;
        default:
            context->failed = true;
            return 0;
    }
}

#define VXML_SINT64_UPPER_BOUND 9223372036854775808.0
#define VXML_UINT64_UPPER_BOUND 18446744073709551616.0

static int expr_truthy(void *user, const qvm_value_t *value) {
    expr_eval_context *context = (expr_eval_context *)user;
    if (value == NULL || value->type != VXML_CMETA_VALUE_BOOL) {
        if (context != NULL) context->failed = true;
        return 0;
    }
    return value->boolean != 0;
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
    if (number >= VXML_SINT64_UPPER_BOUND) {
        *out_order = -1;
        return;
    }
    if (number < -VXML_SINT64_UPPER_BOUND) {
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
    if (number >= VXML_UINT64_UPPER_BOUND) {
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
                            const qvm_value_t *right,
                            int *out_order, bool *out_unordered) {
    *out_unordered = false;
    if (!value_is_numeric((vxml_cmeta_value_kind)left->type) ||
        !value_is_numeric((vxml_cmeta_value_kind)right->type))
        return false;
    if (left->type == VXML_CMETA_VALUE_FLOAT &&
        right->type == VXML_CMETA_VALUE_FLOAT) {
        if (isnan(left->number) || isnan(right->number)) {
            *out_unordered = true;
            *out_order = 0;
        } else {
            *out_order = left->number < right->number ? -1 :
                         left->number > right->number ? 1 : 0;
        }
        return true;
    }
    if (right->type == VXML_CMETA_VALUE_FLOAT) {
        if (left->type == VXML_CMETA_VALUE_SINT)
            compare_sint_float(left->integer, right->number,
                               out_order, out_unordered);
        else
            compare_uint_float(left->uinteger, right->number,
                               out_order, out_unordered);
        return true;
    }
    if (left->type == VXML_CMETA_VALUE_FLOAT) {
        if (right->type == VXML_CMETA_VALUE_SINT)
            compare_sint_float(right->integer, left->number,
                               out_order, out_unordered);
        else
            compare_uint_float(right->uinteger, left->number,
                               out_order, out_unordered);
        *out_order = -*out_order;
        return true;
    }
    if (left->type == VXML_CMETA_VALUE_SINT &&
        right->type == VXML_CMETA_VALUE_SINT) {
        *out_order = left->integer < right->integer ? -1 :
                     left->integer > right->integer ? 1 : 0;
        return true;
    }
    if (left->type == VXML_CMETA_VALUE_UINT &&
        right->type == VXML_CMETA_VALUE_UINT) {
        *out_order = left->uinteger < right->uinteger ? -1 :
                     left->uinteger > right->uinteger ? 1 : 0;
        return true;
    }
    if (left->type == VXML_CMETA_VALUE_SINT) {
        *out_order = left->integer < 0 ? -1 :
                     (uint64_t)left->integer < right->uinteger ? -1 :
                     (uint64_t)left->integer > right->uinteger ? 1 : 0;
        return true;
    }
    *out_order = right->integer < 0 ? 1 :
                 left->uinteger < (uint64_t)right->integer ? -1 :
                 left->uinteger > (uint64_t)right->integer ? 1 : 0;
    return true;
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
                (right < 0 && left < INT64_MIN - right)) return false;
            *out = left + right;
            return true;
        case QVM_OP_SUB:
            if ((right > 0 && left < INT64_MIN + right) ||
                (right < 0 && left > INT64_MAX + right)) return false;
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
        default:
            return false;
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
        default:
            return false;
    }
}

static bool numeric_as_double(const qvm_value_t *value, double *out) {
    if (value->type == VXML_CMETA_VALUE_FLOAT)
        *out = value->number;
    else if (value->type == VXML_CMETA_VALUE_SINT)
        *out = (double)value->integer;
    else if (value->type == VXML_CMETA_VALUE_UINT)
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
        (op == QVM_OP_DIV && right_number == 0.0))
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

static int arithmetic_binary(qvm_opcode_t op, const qvm_value_t *left,
                             const qvm_value_t *right, qvm_value_t *out) {
    if (left->type == VXML_CMETA_VALUE_SINT &&
        right->type == VXML_CMETA_VALUE_SINT) {
        int64_t result;
        if (!evaluate_sint_arithmetic(
                op, left->integer, right->integer, &result))
            return 0;
        make_qvm_value(out, VXML_CMETA_VALUE_SINT);
        out->integer = result;
        return 1;
    }
    if (left->type == VXML_CMETA_VALUE_UINT &&
        right->type == VXML_CMETA_VALUE_UINT) {
        uint64_t result;
        if (!evaluate_uint_arithmetic(
                op, left->uinteger, right->uinteger, &result))
            return 0;
        make_qvm_value(out, VXML_CMETA_VALUE_UINT);
        out->uinteger = result;
        return 1;
    }
    if (left->type == VXML_CMETA_VALUE_FLOAT ||
        right->type == VXML_CMETA_VALUE_FLOAT) {
        double result;
        if (!evaluate_float_arithmetic(op, left, right, &result)) return 0;
        make_qvm_value(out, VXML_CMETA_VALUE_FLOAT);
        out->number = result;
        return 1;
    }
    return 0;
}

static int expr_binary(void *user, qvm_opcode_t op, uint32_t arg,
                       const qvm_value_t *left, const qvm_value_t *right,
                       qvm_value_t *out) {
    expr_eval_context *context = (expr_eval_context *)user;
    int order = 0;
    bool unordered = false;
    bool result;
    int ok = 0;
    if (op == QVM_OP_ADD || op == QVM_OP_SUB || op == QVM_OP_MUL ||
        op == QVM_OP_DIV || op == QVM_OP_MOD) {
        ok = arithmetic_binary(op, left, right, out);
    } else if (op == QVM_OP_BAND || op == QVM_OP_BOR) {
        if (left->type == VXML_CMETA_VALUE_BOOL &&
            right->type == VXML_CMETA_VALUE_BOOL) {
            const bool logical = op == QVM_OP_BAND
                ? (left->boolean && right->boolean)
                : (left->boolean || right->boolean);
            make_qvm_value(out, VXML_CMETA_VALUE_BOOL);
            out->boolean = logical;
            ok = 1;
        }
    } else if (op == QVM_OP_CMP && arg <= 5u) {
        if (left->type == VXML_CMETA_VALUE_BOOL &&
            right->type == VXML_CMETA_VALUE_BOOL && arg <= 1u) {
            order = left->boolean == right->boolean ? 0 :
                    left->boolean ? 1 : -1;
            ok = 1;
        } else if (left->type == VXML_CMETA_VALUE_STRING &&
                   right->type == VXML_CMETA_VALUE_STRING) {
            const size_t common = left->length < right->length
                ? left->length : right->length;
            order = common != 0u ? memcmp(left->str, right->str, common) : 0;
            if (order == 0)
                order = left->length < right->length ? -1 :
                        left->length > right->length ? 1 : 0;
            ok = 1;
        } else if (numeric_compare(
                       left, right, &order, &unordered)) {
            ok = 1;
        }
        if (ok) {
            result = arg == 0u ? (!unordered && order == 0) :
                     arg == 1u ? (unordered || order != 0) :
                     arg == 2u ? (!unordered && order < 0) :
                     arg == 3u ? (!unordered && order <= 0) :
                     arg == 4u ? (!unordered && order > 0) :
                                 (!unordered && order >= 0);
            make_qvm_value(out, VXML_CMETA_VALUE_BOOL);
            out->boolean = result;
        }
    }
    if (!ok && context != NULL) context->failed = true;
    return ok;
}

static int expr_unary(void *user, qvm_opcode_t op,
                      const qvm_value_t *input, qvm_value_t *out) {
    expr_eval_context *context = (expr_eval_context *)user;
    if (op == QVM_OP_NEG && input != NULL && out != NULL) {
        if (input->type == VXML_CMETA_VALUE_SINT &&
            input->integer != INT64_MIN) {
            const int64_t result = -input->integer;
            make_qvm_value(out, VXML_CMETA_VALUE_SINT);
            out->integer = result;
            return 1;
        }
        if (input->type == VXML_CMETA_VALUE_FLOAT &&
            isfinite(-input->number)) {
            const double result = -input->number;
            make_qvm_value(out, VXML_CMETA_VALUE_FLOAT);
            out->number = result;
            return 1;
        }
    }
    if (context != NULL) context->failed = true;
    return 0;
}

static void expr_make_invalid(void *user, qvm_value_t *out) {
    (void)user;
    make_qvm_value(out, VXML_CMETA_VALUE_UNDEFINED);
}

static void expr_make_bool(void *user, int value, qvm_value_t *out) {
    (void)user;
    make_qvm_value(out, VXML_CMETA_VALUE_BOOL);
    out->boolean = value != 0;
}

static void expr_make_number(void *user, double value, qvm_value_t *out) {
    (void)user;
    make_qvm_value(out, VXML_CMETA_VALUE_FLOAT);
    out->number = value;
}

static void expr_make_string(void *user, const char *value, size_t size,
                             qvm_value_t *out) {
    (void)user;
    make_qvm_value(out, VXML_CMETA_VALUE_STRING);
    out->str = value;
    out->length = size;
}

vxml_status vxml_cmeta_expr_evaluate(
    const vxml_cmeta_expr_program *program,
    const vxml_cmeta_expr_runtime *runtime,
    vxml_cmeta_expr_scratch *scratch,
    vxml_cmeta_value_view *out_value,
    vxml_cmeta_expr_diagnostic *diagnostic) {
    const vxml_cmeta_expr_program_impl *impl = program != NULL
        ? (const vxml_cmeta_expr_program_impl *)program->impl : NULL;
    expr_eval_context context;
    qvm_exec_ops_t ops = {0};
    qvm_value_t result = {0};
    qvm_diagnostic_t qvm_diagnostic = {0};
    int status;
    if (out_value != NULL) memset(out_value, 0, sizeof(*out_value));
    if (scratch != NULL) scratch->used = 0u;
    if (impl == NULL || out_value == NULL || scratch == NULL ||
        (scratch->capacity != 0u && scratch->bytes == NULL) ||
        !runtime_valid(impl, runtime))
        return expr_report(diagnostic, VXML_INVALID_ARGUMENT, 0u,
                           "invalid VoiceXML CMeta evaluation arguments");
    if (scratch->capacity < impl->scratch_bytes)
        return expr_report(diagnostic, VXML_LIMIT_EXCEEDED, 0u,
                           "VoiceXML CMeta evaluation scratch is too small");
    context.program = impl;
    context.runtime = runtime;
    context.scratch = scratch;
    context.failure_status = VXML_SEMANTIC_ERROR;
    context.failed = false;
    ops.resolve = expr_resolve;
    ops.truthy = expr_truthy;
    ops.binary = expr_binary;
    ops.unary = expr_unary;
    ops.make_invalid = expr_make_invalid;
    ops.make_bool = expr_make_bool;
    ops.make_number = expr_make_number;
    ops.make_string = expr_make_string;
    status = qvm_execute_ex(
        impl->instructions, impl->instruction_count, 0u,
        impl->instruction_count, &ops, &context, NULL, &result,
        &impl->qvm_limits, &qvm_diagnostic);
    if (context.failed || status != QVM_STATUS_OK)
        return expr_report(
            diagnostic, context.failed
                ? context.failure_status : VXML_SEMANTIC_ERROR,
            qvm_diagnostic.instruction == QVM_NO_INSTRUCTION
                ? 0u : qvm_diagnostic.instruction,
            context.failed
                ? "VoiceXML CMeta expression evaluation failed"
                : qvm_diagnostic.message);
    if (result.type == VXML_CMETA_VALUE_UNDEFINED) {
        if (impl->condition || !impl->direct_location)
            return expr_report(diagnostic, VXML_SEMANTIC_ERROR, 0u,
                               "undefined cannot be used by this expression");
        out_value->kind = VXML_CMETA_VALUE_UNDEFINED;
        return expr_report(diagnostic, VXML_OK, 0u, NULL);
    }
    if (result.type != (int)impl->result_kind)
        return expr_report(diagnostic, VXML_SEMANTIC_ERROR, 0u,
                           "CMeta expression result type changed at runtime");
    out_value->kind = (vxml_cmeta_value_kind)result.type;
    switch (out_value->kind) {
        case VXML_CMETA_VALUE_BOOL:
            out_value->data.boolean = result.boolean != 0;
            break;
        case VXML_CMETA_VALUE_SINT:
            out_value->data.sint = result.integer;
            break;
        case VXML_CMETA_VALUE_UINT:
            out_value->data.uint_value = result.uinteger;
            break;
        case VXML_CMETA_VALUE_FLOAT:
            out_value->data.number = result.number;
            break;
        case VXML_CMETA_VALUE_STRING:
            out_value->data.string.data = result.str;
            out_value->data.string.size = result.length;
            break;
        default:
            return expr_report(diagnostic, VXML_SEMANTIC_ERROR, 0u,
                               "CMeta expression produced an invalid scalar");
    }
    return expr_report(diagnostic, VXML_OK, 0u, NULL);
}

void vxml_cmeta_expr_program_destroy(vxml_cmeta_expr_program *program) {
    if (program == NULL || program->impl == NULL) return;
    expr_program_impl_destroy(
        (vxml_cmeta_expr_program_impl *)program->impl);
    program->impl = NULL;
}
