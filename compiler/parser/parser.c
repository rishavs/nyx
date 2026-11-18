#include "parser_internal.h"

#include "limits_config.h"

bool token_opens_nesting(TokenType type)
{
    return type == TOKEN_LPAREN || type == TOKEN_LBRACKET ||
           type == TOKEN_LBRACE || type == TOKEN_KEYWORD_IF ||
           type == TOKEN_KEYWORD_WHILE || type == TOKEN_KEYWORD_FOR ||
           type == TOKEN_KEYWORD_MATCH || type == TOKEN_KEYWORD_FUN ||
           type == TOKEN_KEYWORD_METHOD || type == TOKEN_KEYWORD_STATIC ||
           type == TOKEN_KEYWORD_TYPE || type == TOKEN_KEYWORD_SCOPE ||
           type == TOKEN_KEYWORD_DEFER || type == TOKEN_KEYWORD_PARALLEL ||
           type == TOKEN_KEYWORD_WHEN;
}

bool token_closes_nesting(TokenType type)
{
    return type == TOKEN_RPAREN || type == TOKEN_RBRACKET ||
           type == TOKEN_RBRACE || type == TOKEN_KEYWORD_END;
}

bool token_starts_recovery_statement(const TokenList *tokens, size_t cursor)
{
    const Token *name = token_at(tokens, cursor);
    const Token *next = token_at(tokens, cursor + 1);

    return name != NULL && name->type == TOKEN_IDENTIFIER && next != NULL &&
           (next->type == TOKEN_LPAREN || assignment_ahead(tokens, cursor) ||
            index_assignment_ahead(tokens, cursor) ||
            field_assignment_ahead(tokens, cursor));
}

static bool token_starts_declaration(TokenType type)
{
    return type == TOKEN_KEYWORD_VAR || type == TOKEN_KEYWORD_CONST ||
           type == TOKEN_KEYWORD_GLOBAL || type == TOKEN_KEYWORD_TYPE ||
           type == TOKEN_KEYWORD_FUN || type == TOKEN_KEYWORD_METHOD ||
           type == TOKEN_KEYWORD_STATIC;
}

static bool check_parse_nesting(const TokenList *tokens, ErrorState *errors)
{
    size_t depth = 0;
    size_t unary_depth = 0;
    size_t i;

    for (i = 0; i < tokens->length; i++) {
        const Token *token = list_get(tokens, i);

        if (token_opens_nesting(token->type)) {
            depth++;
            if (depth > NYX_MAX_PARSE_NESTING) {
                error_report_at(errors, token->pos,
                                MESSAGE_PARSER_NESTING_LIMIT,
                                NYX_MAX_PARSE_NESTING);
                return false;
            }
        } else if (token_closes_nesting(token->type)) {
            if (depth > 0) {
                depth--;
            }
        }

        if (token->type == TOKEN_MINUS || token->type == TOKEN_BANG ||
            token->type == TOKEN_TILDE) {
            unary_depth++;
            if (unary_depth > NYX_MAX_PARSE_NESTING) {
                error_report_at(errors, token->pos,
                                MESSAGE_PARSER_NESTING_LIMIT,
                                NYX_MAX_PARSE_NESTING);
                return false;
            }
        } else {
            unary_depth = 0;
        }
    }
    return true;
}


const Token *token_at(const TokenList *tokens, size_t index)
{
    return list_get(tokens, index);
}

SourcePos position_at(const TokenList *tokens, size_t index)
{
    const Token *token = token_at(tokens, index);
    SourcePos pos = {0, 1, 1};

    if (token != NULL) {
        pos = token->pos;
    }
    return pos;
}


bool intern_token(Ast *ast, const Token *token, size_t *out_index,
                         ErrorState *errors)
{
    *out_index = ast_intern_symbol(ast, token->text, token->length);
    if (*out_index == AST_NO_NODE) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME);
        return false;
    }
    return true;
}


bool push_node(Ast *ast, const AstNode *node, size_t *out_index)
{
    *out_index = ast->nodes.length;
    return list_push(&ast->nodes, node);
}


/* Push a top-level declaration node and record its index in the root list. */
bool push_root(Ast *ast, const AstNode *node, ErrorState *errors)
{
    size_t index = ast->nodes.length;

    if (!list_push(&ast->nodes, node)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return false;
    }
    if (!list_push(&ast->roots, &index)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return false;
    }
    return true;
}

static bool parser_parse_step(ErrorState *errors, bool *had_errors, size_t start, const TokenList *tokens, size_t *cursor, const Token **token)
{
    if (errors->fatal ||
        errors->length >= ERROR_MESSAGE_CAPACITY - 1) {
        return false;
    }
    (*had_errors) = true;
    {
        size_t scan = start;
        size_t depth = 0;
        SourcePos failure = position_at(tokens, (*cursor));

        while (((*token) = token_at(tokens, scan)) != NULL &&
               (*token)->type != TOKEN_EOF) {
            if (scan >= (*cursor) && scan > start &&
                (depth == 0 || (*token)->pos.line > failure.line) &&
                (token_starts_declaration((*token)->type) ||
                 token_starts_recovery_statement(tokens, scan))) {
                break;
            }
            if (token_opens_nesting((*token)->type)) {
                depth++;
            } else if (token_closes_nesting((*token)->type) && depth > 0) {
                depth--;
            }
            scan++;
        }
        (*cursor) = scan;
        (*token) = token_at(tokens, (*cursor));
    }
    return true;
}

static bool parser_parse_token(Ast *ast, const TokenList *tokens, ErrorState *errors, const Token **token, size_t *cursor, bool *had_errors)
{
    (*cursor) = 0;
    (*had_errors) = false;

    ast_init(ast);

    if (!check_parse_nesting(tokens, errors)) {
        return false;
    }

    (*token) = token_at(tokens, (*cursor));
    if ((*token) == NULL || (*token)->type == TOKEN_EOF) {
        error_report_at(errors, position_at(tokens, (*cursor)),
                        MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
        return false;
    }
    return true;
}

/* A declaration that starts at the token at *cursor, which names its kind. The
   parser consumes the keyword and records the declaration as a root. */
typedef bool (*DeclarationParser)(const TokenList *tokens,
                                  const List *string_bytes, Ast *ast,
                                  size_t *cursor, const Token *token,
                                  ErrorState *errors);

static bool declare_variable(const TokenList *tokens, const List *string_bytes,
                             Ast *ast, size_t *cursor, const Token *token,
                             ErrorState *errors)
{
    bool is_const = token->type == TOKEN_KEYWORD_CONST;

    (*cursor)++;
    return parse_var_declaration(tokens, string_bytes, ast, cursor, token->pos,
                                 false, is_const, false, NULL, errors);
}

static bool declare_global(const TokenList *tokens, const List *string_bytes,
                           Ast *ast, size_t *cursor, const Token *token,
                           ErrorState *errors)
{
    (*cursor)++;
    return parse_var_declaration(tokens, string_bytes, ast, cursor, token->pos,
                                 true, false, false, NULL, errors);
}

static bool declare_type(const TokenList *tokens, const List *string_bytes,
                         Ast *ast, size_t *cursor, const Token *token,
                         ErrorState *errors)
{
    (*cursor)++;
    return parse_type_declaration(tokens, string_bytes, ast, cursor,
                                  token->pos, errors);
}

static bool declare_function(const TokenList *tokens, const List *string_bytes,
                             Ast *ast, size_t *cursor, const Token *token,
                             ErrorState *errors)
{
    (*cursor)++;
    return parse_function_declaration(tokens, string_bytes, ast, cursor,
                                      token->pos, errors);
}

static bool declare_method(const TokenList *tokens, const List *string_bytes,
                           Ast *ast, size_t *cursor, const Token *token,
                           ErrorState *errors)
{
    bool is_static = token->type == TOKEN_KEYWORD_STATIC;

    (*cursor)++;
    return parse_method_declaration(tokens, string_bytes, ast, cursor,
                                    token->pos, is_static, errors);
}

static const struct {
    TokenType token;
    DeclarationParser parse;
} declarations[] = {
    {TOKEN_KEYWORD_VAR, declare_variable},
    {TOKEN_KEYWORD_CONST, declare_variable},
    {TOKEN_KEYWORD_GLOBAL, declare_global},
    {TOKEN_KEYWORD_TYPE, declare_type},
    {TOKEN_KEYWORD_FUN, declare_function},
    {TOKEN_KEYWORD_METHOD, declare_method},
    {TOKEN_KEYWORD_STATIC, declare_method},
};

typedef enum {
    ROOT_PARSED,
    ROOT_RECOVER, /* an error is reported; skip to the next declaration */
    ROOT_ABORT    /* out of memory */
} RootResult;

/* One top-level item: a declaration, or a statement kept as a root. */
static RootResult parse_root(const TokenList *tokens, const List *string_bytes,
                             Ast *ast, size_t *cursor, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    size_t statement;
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(declarations); i++) {
        if (token->type == declarations[i].token) {
            return declarations[i].parse(tokens, string_bytes, ast, cursor,
                                         token, errors)
                       ? ROOT_PARSED
                       : ROOT_RECOVER;
        }
    }
    switch (parse_shared_statement(tokens, string_bytes, ast, cursor,
                                   &statement, errors)) {
    case STATEMENT_NONE:
        error_report_at(errors, token->pos,
                        MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
        return ROOT_RECOVER;
    case STATEMENT_FAILED:
        return ROOT_RECOVER;
    case STATEMENT_PARSED:
        break;
    }
    if (!list_push(&ast->roots, &statement)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return ROOT_ABORT;
    }
    return ROOT_PARSED;
}

bool parser_parse(const TokenList *tokens, const List *string_bytes, Ast *ast,
                  ErrorState *errors)
{
    const Token *token = {0};
    size_t cursor = {0};
    bool had_errors = {0};

    if (!parser_parse_token(ast, tokens, errors, &token, &cursor, &had_errors)) {
        return false;
    }
    while (token != NULL && token->type != TOKEN_EOF) {
        size_t start = cursor;
        size_t error_start = errors->length;
        RootResult result = parse_root(tokens, string_bytes, ast, &cursor,
                                       errors);

        if (result == ROOT_ABORT) {
            return false;
        }
        if (result == ROOT_PARSED) {
            had_errors = had_errors || errors->length > error_start;
            token = token_at(tokens, cursor);
            continue;
        }
        if (errors->length == error_start) {
            error_report_fatal(errors,
                               MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        }
        if (!parser_parse_step(errors, &had_errors, start, tokens, &cursor,
                               &token)) {
            return false;
        }
    }
    return !had_errors;
}
