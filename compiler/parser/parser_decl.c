#include "parser_internal.h"

bool parse_type_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, ErrorState *errors
)
{
    const Token *token;
    size_t name_index;
    size_t type_parameters_start;
    size_t type_parameters_count;
    size_t children_start;
    size_t i;
    List fields;
    AstNode declaration = {0};

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
        return false;
    }
    if (!intern_token(ast, token, &name_index, errors)) {
        return false;
    }
    (*cursor)++;

    if (!parse_type_parameters(tokens, string_bytes, ast, cursor,
                               &type_parameters_start,
                               &type_parameters_count, errors)) {
        return false;
    }

    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_EQUALS) {
        size_t aliased_type;
        AstNode alias = {0};

        (*cursor)++;
        if (!parse_type(tokens, string_bytes, ast, cursor, &aliased_type, errors)) {
            return false;
        }
        alias.kind = AST_TYPE_ALIAS;
        alias.pos = declaration_pos;
        alias.name_index = name_index;
        alias.left_child = AST_NO_NODE;
        alias.right_child = aliased_type;
        alias.type_parameters_start = type_parameters_start;
        alias.type_parameters_count = type_parameters_count;
        return push_root(ast, &alias, errors);
    }

    if (token == NULL || token->type == TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
        return false;
    }

    list_init(&fields, sizeof(size_t));
    for (;;) {
        size_t field_name_index;
        size_t field_type_index;
        size_t field_index;
        AstNode field = {0};

        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FIELD_NAME);
            list_free(&fields);
            return false;
        }
        field.pos = token->pos;
        if (!intern_token(ast, token, &field_name_index, errors)) {
            list_free(&fields);
            return false;
        }
        (*cursor)++;

        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_COLON) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FIELD_COLON);
            list_free(&fields);
            return false;
        }
        (*cursor)++;

        token = token_at(tokens, *cursor);
        if (token == NULL ||
            (token->type != TOKEN_IDENTIFIER &&
             token->type != TOKEN_LPAREN)) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
            list_free(&fields);
            return false;
        }
        if (!parse_type(tokens, string_bytes, ast, cursor, &field_type_index, errors)) {
            list_free(&fields);
            return false;
        }

        field.kind = AST_FIELD;
        field.name_index = field_name_index;
        field.left_child = field_type_index;
        field.right_child = AST_NO_NODE;
        if (!push_node(ast, &field, &field_index) ||
            !list_push(&fields, &field_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&fields);
            return false;
        }

        token = token_at(tokens, *cursor);
        if (token == NULL) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
            list_free(&fields);
            return false;
        }
        if (token->type == TOKEN_KEYWORD_END) {
            (*cursor)++;
            break;
        }
        if (token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
            list_free(&fields);
            return false;
        }
    }

    children_start = ast->children.length;
    for (i = 0; i < fields.length; i++) {
        size_t field_index = *(const size_t *)list_get(&fields, i);

        if (!ast_push_child(ast, field_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&fields);
            return false;
        }
    }
    declaration.children_count = fields.length;
    list_free(&fields);

    declaration.kind = AST_TYPE_DECLARATION;
    declaration.pos = declaration_pos;
    declaration.name_index = name_index;
    declaration.left_child = AST_NO_NODE;
    declaration.right_child = AST_NO_NODE;
    declaration.children_start = children_start;
    declaration.type_parameters_start = type_parameters_start;
    declaration.type_parameters_count = type_parameters_count;
    return push_root(ast, &declaration, errors);
}

bool parse_function_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, ErrorState *errors
)
{
    const Token *token;
    size_t name_index;
    size_t type_parameters_start;
    size_t type_parameters_count;
    size_t children_start;
    size_t return_type_index;
    size_t body_index;
    size_t i;
    List parameters;
    AstNode declaration = {0};

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
        return false;
    }
    if (!intern_token(ast, token, &name_index, errors)) {
        return false;
    }
    (*cursor)++;

    if (!parse_type_parameters(tokens, string_bytes, ast, cursor,
                               &type_parameters_start,
                               &type_parameters_count, errors)) {
        return false;
    }

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_LPAREN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN);
        return false;
    }
    (*cursor)++;

    list_init(&parameters, sizeof(size_t));
    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_RPAREN) {
        (*cursor)++;
    } else {
        for (;;) {
            size_t parameter_name_index;
            size_t parameter_type_index;
            size_t parameter_index;
            AstNode parameter = {0};

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_IDENTIFIER) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_PARAMETER_NAME);
                list_free(&parameters);
                return false;
            }
            parameter.pos = token->pos;
            if (!intern_token(ast, token, &parameter_name_index, errors)) {
                list_free(&parameters);
                return false;
            }
            (*cursor)++;

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_COLON) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_PARAMETER_COLON);
                list_free(&parameters);
                return false;
            }
            (*cursor)++;

            if (!parse_type(tokens, string_bytes, ast, cursor, &parameter_type_index,
                            errors)) {
                list_free(&parameters);
                return false;
            }

            parameter.kind = AST_PARAMETER;
            parameter.name_index = parameter_name_index;
            parameter.left_child = parameter_type_index;
            parameter.right_child = AST_NO_NODE;
            if (!push_node(ast, &parameter, &parameter_index) ||
                !list_push(&parameters, &parameter_index)) {
                error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                list_free(&parameters);
                return false;
            }

            token = token_at(tokens, *cursor);
            if (token == NULL) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
                list_free(&parameters);
                return false;
            }
            if (token->type == TOKEN_COMMA) {
                (*cursor)++;
                continue;
            }
            if (token->type == TOKEN_RPAREN) {
                (*cursor)++;
                break;
            }
            error_report_at(errors, token->pos,
                            MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
            list_free(&parameters);
            return false;
        }
    }

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_COLON) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_RETURN_TYPE);
        list_free(&parameters);
        return false;
    }
    (*cursor)++;

    if (!parse_type(tokens, string_bytes, ast, cursor, &return_type_index, errors)) {
        list_free(&parameters);
        return false;
    }

    if (!parse_block(tokens, string_bytes, ast, cursor, &body_index, errors)) {
        list_free(&parameters);
        return false;
    }
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        list_free(&parameters);
        return false;
    }
    (*cursor)++;

    /* Parameters are appended after the body so a parameter type that is
       itself a function type cannot split the run. */
    children_start = ast->children.length;
    for (i = 0; i < parameters.length; i++) {
        size_t parameter_index = *(const size_t *)list_get(&parameters, i);

        if (!ast_push_child(ast, parameter_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&parameters);
            return false;
        }
    }
    declaration.children_count = parameters.length;
    list_free(&parameters);

    declaration.kind = AST_FUNCTION_DECLARATION;
    declaration.pos = declaration_pos;
    declaration.name_index = name_index;
    declaration.left_child = body_index;
    declaration.right_child = return_type_index;
    declaration.children_start = children_start;
    declaration.type_parameters_start = type_parameters_start;
    declaration.type_parameters_count = type_parameters_count;
    return push_root(ast, &declaration, errors);
}
