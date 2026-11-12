#include "parser_internal.h"


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
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME);
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
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return false;
    }
    if (!list_push(&ast->roots, &index)) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return false;
    }
    return true;
}

bool parser_parse(const TokenList *tokens, const List *string_bytes, Ast *ast,
                  ErrorState *errors)
{
    const Token *token;
    size_t cursor = 0;

    ast_init(ast);

    token = token_at(tokens, cursor);
    if (token == NULL || token->type == TOKEN_EOF) {
        error_report_at(errors, position_at(tokens, cursor),
                        MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
        return false;
    }

    while (token != NULL && token->type != TOKEN_EOF) {
        SourcePos declaration_pos = token->pos;

        if (token->type == TOKEN_KEYWORD_VAR ||
            token->type == TOKEN_KEYWORD_CONST) {
            bool is_const = token->type == TOKEN_KEYWORD_CONST;

            cursor++;
            if (!parse_var_declaration(tokens, string_bytes, ast, &cursor,
                                       declaration_pos, false, is_const, false,
                                       NULL, errors)) {
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_GLOBAL) {
            cursor++;
            if (!parse_var_declaration(tokens, string_bytes, ast, &cursor,
                                       declaration_pos, true, false, false,
                                       NULL, errors)) {
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_TYPE) {
            cursor++;
            if (!parse_type_declaration(tokens, string_bytes, ast, &cursor,
                                        declaration_pos,
                                        errors)) {
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_FUN) {
            cursor++;
            if (!parse_function_declaration(tokens, string_bytes, ast, &cursor,
                                            declaration_pos, errors)) {
                return false;
            }
        } else if (assignment_ahead(tokens, cursor)) {
            size_t statement;

            if (!parse_assignment(tokens, string_bytes, ast, &cursor,
                                  &statement, errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_IF) {
            size_t statement;

            if (!parse_if(tokens, string_bytes, ast, &cursor, &statement,
                          errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_MATCH) {
            size_t statement;

            if (!parse_match(tokens, string_bytes, ast, &cursor, false,
                             &statement, errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_FOR) {
            size_t statement;

            if (!parse_for(tokens, string_bytes, ast, &cursor, &statement,
                           errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (index_assignment_ahead(tokens, cursor)) {
            size_t statement;

            if (!parse_index_assignment(tokens, string_bytes, ast, &cursor,
                                        &statement, errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (field_assignment_ahead(tokens, cursor)) {
            size_t statement;

            if (!parse_field_assignment(tokens, string_bytes, ast, &cursor,
                                        &statement, errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else if (argument_looks_like_expression(token)) {
            size_t statement;

            if (!parse_expression_statement(tokens, string_bytes, ast, &cursor,
                                            &statement,
                                            errors)) {
                return false;
            }
            if (!list_push(&ast->roots, &statement)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        } else {
            error_report_at(errors, token->pos,
                            MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
            return false;
        }
        token = token_at(tokens, cursor);
    }

    return true;
}
