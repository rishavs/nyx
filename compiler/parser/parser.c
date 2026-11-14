#include "parser_internal.h"

#include "limits_config.h"

bool token_opens_nesting(TokenType type)
{
    return type == TOKEN_LPAREN || type == TOKEN_LBRACKET ||
           type == TOKEN_LBRACE || type == TOKEN_KEYWORD_IF ||
           type == TOKEN_KEYWORD_WHILE || type == TOKEN_KEYWORD_FOR ||
           type == TOKEN_KEYWORD_MATCH || type == TOKEN_KEYWORD_FUN ||
           type == TOKEN_KEYWORD_METHOD || type == TOKEN_KEYWORD_STATIC ||
           type == TOKEN_KEYWORD_TYPE;
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
            if (depth > NIPPY_MAX_PARSE_NESTING) {
                error_report_at(errors, token->pos,
                                MESSAGE_PARSER_NESTING_LIMIT,
                                NIPPY_MAX_PARSE_NESTING);
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
            if (unary_depth > NIPPY_MAX_PARSE_NESTING) {
                error_report_at(errors, token->pos,
                                MESSAGE_PARSER_NESTING_LIMIT,
                                NIPPY_MAX_PARSE_NESTING);
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

bool parser_parse(const TokenList *tokens, const List *string_bytes, Ast *ast,
                  ErrorState *errors)
{
    const Token *token;
    size_t cursor = 0;
    bool had_errors = false;

    ast_init(ast);

    if (!check_parse_nesting(tokens, errors)) {
        return false;
    }

    token = token_at(tokens, cursor);
    if (token == NULL || token->type == TOKEN_EOF) {
        error_report_at(errors, position_at(tokens, cursor),
                        MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
        return false;
    }

    while (token != NULL && token->type != TOKEN_EOF) {
        SourcePos declaration_pos = token->pos;
        size_t start = cursor;
        size_t error_start = errors->length;

        if (token->type == TOKEN_KEYWORD_VAR ||
            token->type == TOKEN_KEYWORD_CONST) {
            bool is_const = token->type == TOKEN_KEYWORD_CONST;

            cursor++;
            if (!parse_var_declaration(tokens, string_bytes, ast, &cursor,
                                       declaration_pos, false, is_const, false,
                                       NULL, errors)) {
                goto recover;
            }
        } else if (token->type == TOKEN_KEYWORD_GLOBAL) {
            cursor++;
            if (!parse_var_declaration(tokens, string_bytes, ast, &cursor,
                                       declaration_pos, true, false, false,
                                       NULL, errors)) {
                goto recover;
            }
        } else if (token->type == TOKEN_KEYWORD_TYPE) {
            cursor++;
            if (!parse_type_declaration(tokens, string_bytes, ast, &cursor,
                                        declaration_pos,
                                        errors)) {
                goto recover;
            }
        } else if (token->type == TOKEN_KEYWORD_FUN) {
            cursor++;
            if (!parse_function_declaration(tokens, string_bytes, ast, &cursor,
                                            declaration_pos, errors)) {
                goto recover;
            }
        } else if (token->type == TOKEN_KEYWORD_METHOD ||
                   token->type == TOKEN_KEYWORD_STATIC) {
            bool is_static = token->type == TOKEN_KEYWORD_STATIC;

            cursor++;
            if (!parse_method_declaration(tokens, string_bytes, ast, &cursor,
                                          declaration_pos, is_static, errors)) {
                goto recover;
            }
        } else if (assignment_ahead(tokens, cursor)) {
            size_t statement;

            if (!parse_assignment(tokens, string_bytes, ast, &cursor,
                                  &statement, errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_IF) {
            size_t statement;

            if (!parse_if(tokens, string_bytes, ast, &cursor, &statement,
                          errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_MATCH) {
            size_t statement;

            if (!parse_match(tokens, string_bytes, ast, &cursor, false,
                             &statement, errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_FOR) {
            size_t statement;

            if (!parse_for(tokens, string_bytes, ast, &cursor, &statement,
                           errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_WHILE) {
            size_t statement;

            if (!parse_while(tokens, string_bytes, ast, &cursor, &statement,
                             errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (index_assignment_ahead(tokens, cursor)) {
            size_t statement;

            if (!parse_index_assignment(tokens, string_bytes, ast, &cursor,
                                        &statement, errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (field_assignment_ahead(tokens, cursor)) {
            size_t statement;

            if (!parse_field_assignment(tokens, string_bytes, ast, &cursor,
                                        &statement, errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (argument_looks_like_expression(token)) {
            size_t statement;

            if (!parse_expression_statement(tokens, string_bytes, ast, &cursor,
                                            &statement,
                                            errors)) {
                goto recover;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report_fatal(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else {
            error_report_at(errors, token->pos,
                            MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
            goto recover;
        }
        if (errors->length > error_start) had_errors = true;
        token = token_at(tokens, cursor);
        continue;

recover:
        if (errors->length == error_start) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        }
        if (errors->fatal ||
            errors->length >= ERROR_MESSAGE_CAPACITY - 1) {
            return false;
        }
        had_errors = true;
        {
            size_t scan = start;
            size_t depth = 0;
            SourcePos failure = position_at(tokens, cursor);

            while ((token = token_at(tokens, scan)) != NULL &&
                   token->type != TOKEN_EOF) {
                if (scan >= cursor && scan > start &&
                    (depth == 0 || token->pos.line > failure.line) &&
                    (token_starts_declaration(token->type) ||
                     token_starts_recovery_statement(tokens, scan))) {
                    break;
                }
                if (token_opens_nesting(token->type)) {
                    depth++;
                } else if (token_closes_nesting(token->type) && depth > 0) {
                    depth--;
                }
                scan++;
            }
            cursor = scan;
            token = token_at(tokens, cursor);
        }
    }

    return !had_errors;
}
