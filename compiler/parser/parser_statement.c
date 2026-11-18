#include "parser_internal.h"

/* Statements. Everything that can start a statement is listed in a table: by
   keyword, or by what the next tokens look like. The top level and block
   bodies share the tables; a block adds the statements that only make sense
   inside a function. */

typedef bool (*StatementParser)(const TokenList *tokens,
                                const List *string_bytes, Ast *ast,
                                size_t *cursor, size_t *out_index,
                                ErrorState *errors);

typedef bool (*TokensAhead)(const TokenList *tokens, size_t cursor);

typedef struct {
    TokenType token;
    StatementParser parse;
} KeywordStatement;

typedef struct {
    TokensAhead ahead;
    StatementParser parse;
} PatternStatement;

static bool parse_match_statement(const TokenList *tokens,
                                  const List *string_bytes, Ast *ast,
                                  size_t *cursor, size_t *out_index,
                                  ErrorState *errors)
{
    return parse_match(tokens, string_bytes, ast, cursor, false, out_index,
                       errors);
}

static bool expression_ahead(const TokenList *tokens, size_t cursor)
{
    return argument_looks_like_expression(token_at(tokens, cursor));
}

static const KeywordStatement shared_keywords[] = {
    {TOKEN_KEYWORD_IF, parse_if},
    {TOKEN_KEYWORD_MATCH, parse_match_statement},
    {TOKEN_KEYWORD_SCOPE, parse_scope},
    {TOKEN_KEYWORD_WHILE, parse_while},
    {TOKEN_KEYWORD_DEFER, parse_defer},
    {TOKEN_KEYWORD_PARALLEL, parse_parallel},
    {TOKEN_KEYWORD_WHEN, parse_when},
    {TOKEN_KEYWORD_FOR, parse_for},
};

/* Tried in order: an expression statement is whatever else looks like one. */
static const PatternStatement shared_patterns[] = {
    {assignment_ahead, parse_assignment},
    {index_assignment_ahead, parse_index_assignment},
    {field_assignment_ahead, parse_field_assignment},
    {expression_ahead, parse_expression_statement},
};

StatementOutcome parse_shared_statement(const TokenList *tokens,
                                        const List *string_bytes, Ast *ast,
                                        size_t *cursor, size_t *out_index,
                                        ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(shared_keywords); i++) {
        if (token->type == shared_keywords[i].token) {
            return shared_keywords[i].parse(tokens, string_bytes, ast, cursor,
                                            out_index, errors)
                       ? STATEMENT_PARSED
                       : STATEMENT_FAILED;
        }
    }
    for (i = 0; i < NYX_COUNT_OF(shared_patterns); i++) {
        if (shared_patterns[i].ahead(tokens, *cursor)) {
            return shared_patterns[i].parse(tokens, string_bytes, ast, cursor,
                                            out_index, errors)
                       ? STATEMENT_PARSED
                       : STATEMENT_FAILED;
        }
    }
    return STATEMENT_NONE;
}

static bool parse_local_declaration(const TokenList *tokens,
                                    const List *string_bytes, Ast *ast,
                                    size_t *cursor, size_t *out_index,
                                    ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    bool is_const = token->type == TOKEN_KEYWORD_CONST;

    (*cursor)++;
    return parse_var_declaration(tokens, string_bytes, ast, cursor, token->pos,
                                 false, is_const, true, out_index, errors);
}

/* The tokens a bare `return` may be followed by. */
static const TokenType return_terminators[] = {
    TOKEN_KEYWORD_END, TOKEN_KEYWORD_ELSE, TOKEN_KEYWORD_ELSEIF,
    TOKEN_KEYWORD_WITH, TOKEN_KEYWORD_BREAK, TOKEN_KEYWORD_CONTINUE,
    TOKEN_KEYWORD_VAR, TOKEN_KEYWORD_CONST, TOKEN_KEYWORD_WHILE,
    TOKEN_KEYWORD_FOR, TOKEN_KEYWORD_SCOPE, TOKEN_KEYWORD_DEFER,
    TOKEN_KEYWORD_PARALLEL, TOKEN_KEYWORD_WHEN, TOKEN_KEYWORD_RETURN,
    TOKEN_EOF,
};

static bool ends_return(TokenType type)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(return_terminators); i++) {
        if (return_terminators[i] == type) {
            return true;
        }
    }
    return false;
}

static bool parse_return(const TokenList *tokens, const List *string_bytes,
                         Ast *ast, size_t *cursor, size_t *out_index,
                         ErrorState *errors)
{
    SourcePos pos = position_at(tokens, *cursor);
    size_t expression = AST_NO_NODE;
    const Token *next;
    AstNode node = {0};

    (*cursor)++;
    next = token_at(tokens, *cursor);
    if (next != NULL && !ends_return(next->type) &&
        !parse_expression(tokens, string_bytes, ast, cursor, &expression,
                          errors)) {
        return false;
    }
    node.kind = AST_RETURN;
    node.pos = pos;
    node.left_child = expression;
    node.right_child = AST_NO_NODE;
    node.name_index = AST_NO_NODE;
    return push_node(ast, &node, out_index);
}

static bool parse_jump(const TokenList *tokens, const List *string_bytes,
                       Ast *ast, size_t *cursor, size_t *out_index,
                       ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};

    (void)string_bytes;
    (void)errors;
    (*cursor)++;
    node.kind = token->type == TOKEN_KEYWORD_BREAK ? AST_BREAK : AST_CONTINUE;
    node.pos = token->pos;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.name_index = AST_NO_NODE;
    return push_node(ast, &node, out_index);
}

static const KeywordStatement block_keywords[] = {
    {TOKEN_KEYWORD_VAR, parse_local_declaration},
    {TOKEN_KEYWORD_CONST, parse_local_declaration},
    {TOKEN_KEYWORD_RETURN, parse_return},
    {TOKEN_KEYWORD_BREAK, parse_jump},
    {TOKEN_KEYWORD_CONTINUE, parse_jump},
};

bool parse_statement(const TokenList *tokens, const List *string_bytes,
                     Ast *ast, size_t *cursor, size_t *out_index,
                     ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    StatementOutcome outcome;
    size_t i;

    if (token == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(block_keywords); i++) {
        if (token->type == block_keywords[i].token) {
            return block_keywords[i].parse(tokens, string_bytes, ast, cursor,
                                           out_index, errors);
        }
    }
    outcome = parse_shared_statement(tokens, string_bytes, ast, cursor,
                                     out_index, errors);
    if (outcome == STATEMENT_NONE) {
        error_report_at(errors, token->pos,
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
    }
    return outcome == STATEMENT_PARSED;
}
