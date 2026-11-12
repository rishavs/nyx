#ifndef NIPPY_PARSER_INTERNAL_H
#define NIPPY_PARSER_INTERNAL_H

#include "parser.h"
#include "operator.h"

#include "messages.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* An argument is either a positional expression or a named wrapper. */
typedef struct {
    size_t name_index;
    size_t value_index;
    SourcePos pos;
    bool named;
} Argument;

typedef bool (*ParseLevel)(const TokenList *tokens,
                           const List *string_bytes, Ast *ast,
                           size_t *cursor, size_t *out_index,
                           ErrorState *errors);

typedef struct {
    TokenType token;
    NippyOperator op;
} BinaryOperator;


/* Internal helpers shared by the parser modules. */

const Token *token_at(const TokenList *tokens, size_t index);
SourcePos position_at(const TokenList *tokens, size_t index);
bool intern_token(Ast *ast, const Token *token, size_t *out_index, ErrorState *errors);
bool push_node(Ast *ast, const AstNode *node, size_t *out_index);
bool push_root(Ast *ast, const AstNode *node, ErrorState *errors);
bool parse_type(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_type_primary(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_type_parameters(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, size_t *out_start, size_t *out_count, ErrorState *errors);
bool push_literal(Ast *ast, const Token *token, size_t *out_index);
bool argument_looks_like_expression(const Token *token);
bool parse_call(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool push_bool_literal(Ast *ast, const Token *token, bool value, size_t *out_index);
bool parse_record_literal(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_type_test(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool match_head_ok(const Ast *ast, size_t head);
bool parse_match(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, bool expression_form, size_t *out_index, ErrorState *errors);
bool parse_if_expression(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_primary(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool member_type_arguments_ahead(const TokenList *tokens, size_t cursor);
bool parse_postfix(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_unary(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_binary_level(ParseLevel next, const BinaryOperator *operators,
    size_t operator_count, const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_multiplicative(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_additive(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_shift(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_relational(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_equality(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_bit_and(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_bit_xor(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_bit_or(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_logical_and(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_logical_or(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_expression(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_block(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool wrap_in_block(Ast *ast, size_t statement_index, SourcePos pos, size_t *out_index);
bool make_if_node(Ast *ast, size_t condition, size_t then_block, size_t else_block,
    SourcePos pos, size_t *out_index);
bool parse_if(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_while(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_for(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool assignment_ahead(const TokenList *tokens, size_t cursor);
bool index_assignment_ahead(const TokenList *tokens, size_t cursor);
bool parse_assignment(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_index_assignment(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, size_t *out_index, ErrorState *errors);
bool field_assignment_ahead(const TokenList *tokens, size_t cursor);
bool parse_field_assignment(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_expression_statement(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_statement(const TokenList *tokens, const List *string_bytes, Ast *ast,
    size_t *cursor, size_t *out_index, ErrorState *errors);
bool parse_var_declaration(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, SourcePos declaration_pos, bool is_global,
    bool is_const, bool as_statement, size_t *out_index, ErrorState *errors);
bool parse_type_declaration(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, SourcePos declaration_pos, ErrorState *errors);
bool parse_function_declaration(const TokenList *tokens, const List *string_bytes,
    Ast *ast, size_t *cursor, SourcePos declaration_pos, ErrorState *errors);
bool parser_parse(const TokenList *tokens, const List *string_bytes, Ast *ast,
    ErrorState *errors);

#endif
