#include "parser_internal.h"

static bool parse_default_literal(const TokenList *tokens, const List *string_bytes,
                                  Ast *ast, size_t *cursor, size_t *out,
                                  ErrorState *errors)
{
    const AstNode *value;
    if (!parse_expression(tokens, string_bytes, ast, cursor, out, errors))
        return false;
    value = ast_get_node(ast, *out);
    if (value != NULL && value->kind == AST_UNARY &&
        value->op == NIPPY_OPERATOR_NEG) {
        value = ast_get_node(ast, value->left_child);
        if (value != NULL && (value->kind == AST_INTEGER_LITERAL ||
                              value->kind == AST_FLOAT_LITERAL)) return true;
    } else if (value != NULL &&
               (value->kind == AST_INTEGER_LITERAL || value->kind == AST_FLOAT_LITERAL ||
                value->kind == AST_BOOL_LITERAL || value->kind == AST_NIL_LITERAL ||
                value->kind == AST_STRING_LITERAL)) {
        return true;
    }
    error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_DEFAULT_LITERAL);
    return false;
}

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
        token = token_at(tokens, *cursor);
        if (token != NULL && token->type == TOKEN_EQUALS) {
            (*cursor)++;
            if (!parse_default_literal(tokens, string_bytes, ast, cursor,
                                       &field.right_child, errors)) {
                list_free(&fields);
                return false;
            }
        }
        if (!push_node(ast, &field, &field_index) ||
            !list_push(&fields, &field_index)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
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
        if (token->type == TOKEN_COMMA) {
            (*cursor)++;
            token = token_at(tokens, *cursor);
            if (token != NULL && token->type == TOKEN_KEYWORD_END) {
                (*cursor)++;
                break;
            }
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
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
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


static bool parse_function_tail(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, size_t name_index, size_t owner_name_index,
    size_t owner_type_index, bool is_method, bool is_static, ErrorState *errors
);


bool parse_function_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    size_t name_index;

    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
        return false;
    }
    if (!intern_token(ast, token, &name_index, errors)) {
        return false;
    }
    (*cursor)++;
    return parse_function_tail(tokens, string_bytes, ast, cursor,
                               declaration_pos, name_index, AST_NO_NODE,
                               AST_NO_NODE, false, false, errors);
}


bool parse_method_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, bool is_static, ErrorState *errors
)
{
    const Token *owner = token_at(tokens, *cursor);
    const Token *separator;
    const Token *member;
    size_t owner_name_index;
    size_t owner_type_index;
    size_t method_name_index;

    if (owner == NULL || owner->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
        return false;
    }
    if (!intern_token(ast, owner, &owner_name_index, errors) ||
        !parse_type_primary(tokens, string_bytes, ast, cursor,
                            &owner_type_index, errors)) {
        return false;
    }
    separator = token_at(tokens, *cursor);
    if (separator == NULL ||
        (is_static ? separator->type != TOKEN_DOUBLE_COLON
                   : separator->type != TOKEN_DOT)) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_MEMBER_NAME);
        return false;
    }
    (*cursor)++;
    member = token_at(tokens, *cursor);
    if (member == NULL || member->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
        return false;
    }
    if (!intern_token(ast, member, &method_name_index, errors)) {
        return false;
    }
    (*cursor)++;
    return parse_function_tail(tokens, string_bytes, ast, cursor,
                               declaration_pos, method_name_index,
                               owner_name_index, owner_type_index, !is_static,
                               is_static, errors);
}

static bool parse_function_tail(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, size_t name_index, size_t owner_name_index,
    size_t owner_type_index, bool is_method, bool is_static, ErrorState *errors
)
{
    const Token *token;
    size_t type_parameters_start;
    size_t type_parameters_count;
    size_t children_start;
    size_t return_type_index = AST_NO_NODE;
    size_t body_index;
    size_t i;
    List parameters;
    AstNode declaration = {0};

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
                error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
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
    if (token != NULL && token->type == TOKEN_COLON) {
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (is_method && token != NULL && token->type == TOKEN_IDENTIFIER &&
            token->length == 4 && memcmp(token->text, "self", 4) == 0) {
            return_type_index = owner_type_index;
            (*cursor)++;
        } else if (!parse_type(tokens, string_bytes, ast, cursor,
                               &return_type_index, errors)) {
            list_free(&parameters);
            return false;
        }
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
    if (is_method) {
        AstNode self_parameter = {0};
        size_t self_name_index;
        self_name_index = ast_intern_symbol(ast, "self", 4);
        if (self_name_index == AST_NO_NODE) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME);
            list_free(&parameters);
            return false;
        }
        self_parameter.kind = AST_PARAMETER;
        self_parameter.pos = declaration_pos;
        self_parameter.name_index = self_name_index;
        self_parameter.left_child = owner_type_index;
        self_parameter.right_child = AST_NO_NODE;
        self_parameter.is_self = true;
        if (!push_node(ast, &self_parameter, &i) ||
            !ast_push_child(ast, i)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&parameters);
            return false;
        }
        declaration.children_count = 1;
    }
    for (i = 0; i < parameters.length; i++) {
        size_t parameter_index = *(const size_t *)list_get(&parameters, i);

        if (!ast_push_child(ast, parameter_index)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&parameters);
            return false;
        }
    }
    declaration.children_count += parameters.length;
    list_free(&parameters);

    declaration.kind = AST_FUNCTION_DECLARATION;
    declaration.pos = declaration_pos;
    declaration.name_index = name_index;
    declaration.second_name_index = owner_name_index;
    declaration.owner_type_index = owner_type_index;
    declaration.is_method = is_method;
    declaration.is_static = is_static;
    declaration.left_child = body_index;
    declaration.right_child = return_type_index;
    declaration.children_start = children_start;
    declaration.type_parameters_start = type_parameters_start;
    declaration.type_parameters_count = type_parameters_count;
    return push_root(ast, &declaration, errors);
}
