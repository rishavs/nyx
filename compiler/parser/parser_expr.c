#include "parser_internal.h"


bool push_literal(Ast *ast, const Token *token, size_t *out_index)
{
    AstNode node = {0};

    node.kind = token->type == TOKEN_FLOAT ? AST_FLOAT_LITERAL
                                            : AST_INTEGER_LITERAL;
    node.pos = token->pos;
    node.integer_value = token->integer_value;
    node.integer_value_u = token->integer_value_u;
    node.float_value = token->float_value;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;

    return push_node(ast, &node, out_index);
}


bool argument_looks_like_expression(const Token *token)
{
    return token != NULL &&
           (token->type == TOKEN_INTEGER ||
            token->type == TOKEN_FLOAT ||
            token->type == TOKEN_STRING ||
            token->type == TOKEN_IDENTIFIER ||
            token->type == TOKEN_KEYWORD_TRUE ||
            token->type == TOKEN_KEYWORD_FALSE ||
            token->type == TOKEN_KEYWORD_NIL ||
            token->type == TOKEN_KEYWORD_IF ||
            token->type == TOKEN_KEYWORD_TYPEOF ||
            token->type == TOKEN_KEYWORD_MATCH ||
            token->type == TOKEN_BANG ||
            token->type == TOKEN_MINUS ||
            token->type == TOKEN_TILDE ||
            token->type == TOKEN_LPAREN ||
            token->type == TOKEN_LBRACKET ||
            token->type == TOKEN_LBRACE);
}


bool push_bool_literal(Ast *ast, const Token *token, bool value,
                              size_t *out_index)
{
    AstNode node = {0};

    node.kind = AST_BOOL_LITERAL;
    node.pos = token->pos;
    node.integer_value = value ? 1 : 0;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}


bool parse_type_test(const TokenList *tokens, const List *string_bytes,
                            Ast *ast, size_t *cursor, size_t *out_index,
                            ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    size_t value;
    size_t tested_type;
    AstNode node = {0};

    (*cursor)++; /* past typeof */
    if (!parse_expression(tokens, string_bytes, ast, cursor, &value, errors)) {
        return false;
    }
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_IS) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_IS);
        return false;
    }
    (*cursor)++;
    if (!parse_type(tokens, string_bytes, ast, cursor, &tested_type, errors)) {
        return false;
    }
    node.kind = AST_IS;
    node.pos = pos;
    node.name_index = AST_NO_NODE;
    node.left_child = value;
    node.right_child = tested_type;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}


static bool generic_static_owner_ahead(const TokenList *tokens, size_t cursor)
{
    const Token *token = token_at(tokens, cursor + 1);
    size_t depth = 0;
    size_t i;

    if (token == NULL || token->type != TOKEN_LT) {
        return false;
    }
    for (i = cursor + 1; (token = token_at(tokens, i)) != NULL; i++) {
        if (token->type == TOKEN_LT) {
            if (++depth > 256) {
                return false;
            }
        } else if (token->type == TOKEN_GT) {
            if (--depth == 0) {
                token = token_at(tokens, i + 1);
                return token != NULL && token->type == TOKEN_DOUBLE_COLON;
            }
        } else if (token->type != TOKEN_IDENTIFIER &&
                   token->type != TOKEN_INTEGER &&
                   token->type != TOKEN_COMMA &&
                   token->type != TOKEN_PIPE &&
                   token->type != TOKEN_LPAREN &&
                   token->type != TOKEN_RPAREN &&
                   token->type != TOKEN_COLON) {
            break;
        }
    }
    return false;
}


bool parse_primary(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};

    if (token == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_EXPRESSION);
        return false;
    }

    if (token->type == TOKEN_KEYWORD_TYPEOF) {
        return parse_type_test(tokens, string_bytes, ast, cursor, out_index,
                               errors);
    }

    if (token->type == TOKEN_KEYWORD_IF) {
        return parse_if_expression(tokens, string_bytes, ast, cursor,
                                   out_index, errors);
    }

    if (token->type == TOKEN_KEYWORD_MATCH) {
        return parse_match(tokens, string_bytes, ast, cursor, true, out_index,
                           errors);
    }

    if (token->type == TOKEN_LBRACE) {
        return parse_record_literal(tokens, string_bytes, ast, cursor,
                                    out_index, errors);
    }
    if (token->type == TOKEN_INTEGER || token->type == TOKEN_FLOAT) {
        if (!push_literal(ast, token, out_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
        (*cursor)++;
        return true;
    }
    if (token->type == TOKEN_LPAREN) {
        size_t inner;

        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor, &inner, errors)) {
            return false;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_RPAREN) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_RPAREN);
            return false;
        }
        (*cursor)++;
        *out_index = inner;
        return true;
    }
    if (token->type == TOKEN_KEYWORD_TRUE ||
        token->type == TOKEN_KEYWORD_FALSE) {
        if (!push_bool_literal(ast, token, token->type == TOKEN_KEYWORD_TRUE,
                               out_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
        (*cursor)++;
        return true;
    }
    if (token->type == TOKEN_KEYWORD_NIL) {
        node.kind = AST_NIL_LITERAL;
        node.pos = token->pos;
        node.name_index = AST_NO_NODE;
        node.left_child = AST_NO_NODE;
        node.right_child = AST_NO_NODE;
        (*cursor)++;
        return push_node(ast, &node, out_index);
    }
    if (token->type == TOKEN_LBRACKET) {
        size_t children_start;
        size_t count = 0;
        SourcePos list_pos = token->pos;
        List elements;

        (*cursor)++;
        list_init(&elements, sizeof(size_t));
        token = token_at(tokens, *cursor);
        if (token != NULL && token->type == TOKEN_RBRACKET) {
            (*cursor)++;
        } else {
            for (;;) {
                size_t element;

                if (!parse_expression(tokens, string_bytes, ast, cursor,
                                      &element, errors)) {
                    list_free(&elements);
                    return false;
                }
                if (!list_push(&elements, &element)) {
                    error_report(errors,
                                 MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                    list_free(&elements);
                    return false;
                }
                count++;
                token = token_at(tokens, *cursor);
                if (token == NULL || token->type == TOKEN_EOF) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_LIST_RBRACKET);
                    list_free(&elements);
                    return false;
                }
                if (token->type == TOKEN_COMMA) {
                    (*cursor)++;
                    continue;
                }
                if (token->type == TOKEN_RBRACKET) {
                    (*cursor)++;
                    break;
                }
                error_report_at(errors, token->pos,
                                MESSAGE_PARSER_EXPECTED_LIST_SEPARATOR);
                list_free(&elements);
                return false;
            }
        }
        children_start = ast->children.length;
        if (count > 0) {
            size_t i;

            for (i = 0; i < count; i++) {
                size_t element = *(const size_t *)list_get(&elements, i);

                if (!ast_push_child(ast, element)) {
                    error_report(errors,
                                 MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                    list_free(&elements);
                    return false;
                }
            }
        }
        node.kind = AST_LIST_LITERAL;
        node.pos = list_pos;
        node.name_index = AST_NO_NODE;
        node.left_child = AST_NO_NODE;
        node.right_child = AST_NO_NODE;
        node.children_start = children_start;
        node.children_count = count;
        list_free(&elements);
        return push_node(ast, &node, out_index);
    }
    if (token->type == TOKEN_STRING) {
        size_t offset = ast->bytes.length;
        size_t i;

        for (i = 0; i < token->string_length; i++) {
            const unsigned char *byte = list_get(
                string_bytes, token->string_offset + i);

            if (byte == NULL || !ast_push_byte(ast, *byte)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
        }
        node.kind = AST_STRING_LITERAL;
        node.pos = token->pos;
        node.name_index = AST_NO_NODE;
        node.left_child = AST_NO_NODE;
        node.right_child = AST_NO_NODE;
        node.children_start = 0;
        node.children_count = 0;
        node.string_offset = offset;
        node.string_length = token->string_length;
        (*cursor)++;
        return push_node(ast, &node, out_index);
    }
    if (token->type == TOKEN_IDENTIFIER) {
        const Token *next = token_at(tokens, *cursor + 1);

        if ((next != NULL && next->type == TOKEN_DOUBLE_COLON) ||
            generic_static_owner_ahead(tokens, *cursor)) {
            size_t owner_name_index;
            size_t owner_type_index = AST_NO_NODE;
            size_t call_index;
            const Token *method;
            const Token *open;
            AstNode *call;

            if (!intern_token(ast, token, &owner_name_index, errors)) {
                return false;
            }
            if (next->type == TOKEN_LT) {
                if (!parse_type_primary(tokens, string_bytes, ast, cursor,
                                        &owner_type_index, errors)) {
                    return false;
                }
                (*cursor)++;
            } else {
                *cursor += 2;
            }
            method = token_at(tokens, *cursor);
            open = token_at(tokens, *cursor + 1);
            if (method == NULL || method->type != TOKEN_IDENTIFIER ||
                open == NULL || open->type != TOKEN_LPAREN) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_ARGUMENT);
                return false;
            }
            if (!parse_call(tokens, string_bytes, ast, cursor, &call_index,
                            errors)) {
                return false;
            }
            call = (AstNode *)list_get(&ast->nodes, call_index);
            if (call == NULL) {
                error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                return false;
            }
            call->is_static = true;
            call->second_name_index = owner_name_index;
            call->owner_type_index = owner_type_index;
            *out_index = call_index;
            return true;
        }
        if (next != NULL && next->type == TOKEN_LPAREN) {
            return parse_call(tokens, string_bytes, ast, cursor, out_index, errors);
        }

        node.kind = AST_IDENTIFIER;
        node.pos = token->pos;
        node.left_child = AST_NO_NODE;
        node.right_child = AST_NO_NODE;
        if (!intern_token(ast, token, &node.name_index, errors)) {
            return false;
        }
        (*cursor)++;
        return push_node(ast, &node, out_index);
    }
    error_report_at(errors, token->pos, MESSAGE_PARSER_EXPECTED_EXPRESSION);
    return false;
}

bool member_type_arguments_ahead(const TokenList *tokens, size_t cursor)
{
    int depth = 0;
    size_t i;

    for (i = cursor; token_at(tokens, i) != NULL; i++) {
        TokenType type = token_at(tokens, i)->type;

        if (type == TOKEN_LT) {
            depth++;
        } else if (type == TOKEN_GT) {
            depth--;
            if (depth == 0) {
                const Token *next = token_at(tokens, i + 1);

                return next != NULL && next->type == TOKEN_LPAREN;
            }
        } else if (type == TOKEN_EOF || depth == 0) {
            return false;
        }
    }
    return false;
}

bool parse_postfix(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    size_t expression;

    if (!parse_primary(tokens, string_bytes, ast, cursor, &expression, errors)) {
        return false;
    }
    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        SourcePos member_pos;

        if (token != NULL && token->type == TOKEN_LBRACKET) {
            AstNode node = {0};
            SourcePos index_pos = token->pos;
            size_t index;

            (*cursor)++;
            if (!parse_expression(tokens, string_bytes, ast, cursor, &index,
                                  errors)) {
                return false;
            }
            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_RBRACKET) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_RBRACKET);
                return false;
            }
            (*cursor)++;
            node.kind = AST_INDEX;
            node.pos = index_pos;
            node.name_index = AST_NO_NODE;
            node.left_child = expression;
            node.right_child = index;
            if (!push_node(ast, &node, &expression)) {
                return false;
            }
            continue;
        }
        if (token == NULL || token->type != TOKEN_DOT) {
            break;
        }
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_MEMBER_NAME);
            return false;
        }
        member_pos = token->pos;

        {
            const Token *after = token_at(tokens, *cursor + 1);
            bool call = after != NULL &&
                        (after->type == TOKEN_LPAREN ||
                         (after->type == TOKEN_LT &&
                          member_type_arguments_ahead(tokens, *cursor + 1)));

            if (!call) {
                AstNode field = {0};
                size_t field_name;

                if (!intern_token(ast, token, &field_name, errors)) {
                    return false;
                }
                (*cursor)++;
                field.kind = AST_MEMBER_ACCESS;
                field.pos = member_pos;
                field.name_index = field_name;
                field.left_child = expression;
                field.right_child = AST_NO_NODE;
                field.children_start = 0;
                field.children_count = 0;
                if (!push_node(ast, &field, &expression)) {
                    return false;
                }
                continue;
            }
        }

        {
            AstNode node = {0};
            size_t name_index;
            size_t type_argument = AST_NO_NODE;
            size_t children_start;
            size_t i;
            List arguments;

            if (!intern_token(ast, token, &name_index, errors)) {
                return false;
            }
            (*cursor)++; /* past the member name */

            token = token_at(tokens, *cursor);
            if (token != NULL && token->type == TOKEN_LT) {
                size_t first_type;
                size_t second_type = AST_NO_NODE;

                (*cursor)++;
                if (!parse_type(tokens, string_bytes, ast, cursor, &first_type,
                                errors)) {
                    return false;
                }
                token = token_at(tokens, *cursor);
                if (token != NULL && token->type == TOKEN_COMMA) {
                    (*cursor)++;
                    if (!parse_type(tokens, string_bytes, ast, cursor,
                                    &second_type, errors)) {
                        return false;
                    }
                    {
                        AstNode pair = {0};

                        pair.kind = AST_TYPE_TABLE;
                        pair.pos = member_pos;
                        pair.name_index = AST_NO_NODE;
                        pair.left_child = first_type;
                        pair.right_child = second_type;
                        pair.children_start = 0;
                        pair.children_count = 0;
                        if (!push_node(ast, &pair, &type_argument)) {
                            return false;
                        }
                    }
                } else {
                    type_argument = first_type;
                }
                token = token_at(tokens, *cursor);
                if (token == NULL || token->type != TOKEN_GT) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT);
                    return false;
                }
                (*cursor)++;
            }

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_LPAREN) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_ARGUMENT);
                return false;
            }
            (*cursor)++;

            /* Only positional arguments exist here: an `IDENT '='` is left
               for the expression parser to stop on, so the separator check
               below reports it as an argument error. */
            list_init(&arguments, sizeof(size_t));
            token = token_at(tokens, *cursor);
            if (token != NULL && token->type == TOKEN_RPAREN) {
                (*cursor)++;
            } else {
                for (;;) {
                    size_t argument;

                    token = token_at(tokens, *cursor);
                    if (!argument_looks_like_expression(token)) {
                        error_report_at(errors, position_at(tokens, *cursor),
                                        MESSAGE_PARSER_EXPECTED_ARGUMENT);
                        list_free(&arguments);
                        return false;
                    }
                    if (!parse_expression(tokens, string_bytes, ast, cursor, &argument,
                                          errors)) {
                        list_free(&arguments);
                        return false;
                    }
                    if (!list_push(&arguments, &argument)) {
                        error_report(errors,
                                     MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                        list_free(&arguments);
                        return false;
                    }

                    token = token_at(tokens, *cursor);
                    if (token == NULL) {
                        error_report_at(errors, position_at(tokens, *cursor),
                                        MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN);
                        list_free(&arguments);
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
                                    MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN);
                    list_free(&arguments);
                    return false;
                }
            }

            children_start = ast->children.length;
            for (i = 0; i < arguments.length; i++) {
                size_t argument = *(const size_t *)list_get(&arguments, i);

                if (!ast_push_child(ast, argument)) {
                    error_report(errors,
                                 MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                    list_free(&arguments);
                    return false;
                }
            }

            node.kind = AST_MEMBER_CALL;
            node.pos = member_pos;
            node.name_index = name_index;
            node.left_child = expression;
            node.right_child = type_argument;
            node.children_start = children_start;
            node.children_count = arguments.length;
            list_free(&arguments);
            if (!push_node(ast, &node, &expression)) {
                return false;
            }
        }
    }
    *out_index = expression;
    return true;
}


bool parse_unary(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};
    size_t operand;

    if (token != NULL && (token->type == TOKEN_BANG ||
                          token->type == TOKEN_MINUS ||
                          token->type == TOKEN_TILDE)) {
        (*cursor)++;
        if (!parse_unary(tokens, string_bytes, ast, cursor, &operand, errors)) {
            return false;
        }
        node.kind = AST_UNARY;
        node.pos = token->pos;
        node.op = token->type == TOKEN_BANG
                      ? NIPPY_OPERATOR_NOT
                      : (token->type == TOKEN_MINUS ? NIPPY_OPERATOR_NEG
                                                    : NIPPY_OPERATOR_BIT_NOT);
        node.name_index = AST_NO_NODE;
        node.left_child = operand;
        node.right_child = AST_NO_NODE;
        return push_node(ast, &node, out_index);
    }
    return parse_postfix(tokens, string_bytes, ast, cursor, out_index, errors);
}


bool parse_binary_level(ParseLevel next, const BinaryOperator *operators,
                               size_t operator_count, const TokenList *tokens,
                               const List *string_bytes, Ast *ast, size_t *cursor,
                               size_t *out_index, ErrorState *errors)
{
    size_t left;

    if (!next(tokens, string_bytes, ast, cursor, &left, errors)) {
        return false;
    }
    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        const BinaryOperator *matched = NULL;
        size_t i;
        size_t right;
        AstNode node = {0};

        if (token == NULL) {
            break;
        }
        for (i = 0; i < operator_count; i++) {
            if (token->type == operators[i].token) {
                matched = &operators[i];
                break;
            }
        }
        if (matched == NULL) {
            break;
        }
        (*cursor)++;
        if (!next(tokens, string_bytes, ast, cursor, &right, errors)) {
            return false;
        }
        node.kind = AST_BINARY;
        node.pos = token->pos;
        node.op = matched->op;
        node.name_index = AST_NO_NODE;
        node.left_child = left;
        node.right_child = right;
        node.children_start = 0;
        node.children_count = 0;
        if (!push_node(ast, &node, &left)) {
            return false;
        }
    }
    *out_index = left;
    return true;
}


bool parse_multiplicative(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_STAR, NIPPY_OPERATOR_MUL},
        {TOKEN_SLASH, NIPPY_OPERATOR_DIV},
        {TOKEN_PERCENT, NIPPY_OPERATOR_MOD}
    };

    return parse_binary_level(parse_unary, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_additive(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_PLUS, NIPPY_OPERATOR_ADD},
        {TOKEN_MINUS, NIPPY_OPERATOR_SUB}
    };

    return parse_binary_level(parse_multiplicative, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_shift(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    size_t left;

    if (!parse_additive(tokens, string_bytes, ast, cursor, &left, errors)) {
        return false;
    }
    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        const Token *next = token_at(tokens, *cursor + 1);
        NippyOperator op;
        size_t right;
        AstNode node = {0};

        if (token == NULL || next == NULL ||
            (token->type != TOKEN_LT && token->type != TOKEN_GT) ||
            next->type != token->type) {
            break;
        }
        op = token->type == TOKEN_LT ? NIPPY_OPERATOR_SHL
                                     : NIPPY_OPERATOR_SHR;
        *cursor += 2;
        if (!parse_additive(tokens, string_bytes, ast, cursor, &right, errors)) {
            return false;
        }
        node.kind = AST_BINARY;
        node.pos = token->pos;
        node.op = op;
        node.name_index = AST_NO_NODE;
        node.left_child = left;
        node.right_child = right;
        node.children_start = 0;
        node.children_count = 0;
        if (!push_node(ast, &node, &left)) {
            return false;
        }
    }
    *out_index = left;
    return true;
}


bool parse_relational(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_LT, NIPPY_OPERATOR_LT},
        {TOKEN_LE, NIPPY_OPERATOR_LE},
        {TOKEN_GT, NIPPY_OPERATOR_GT},
        {TOKEN_GE, NIPPY_OPERATOR_GE}
    };

    return parse_binary_level(parse_bit_or, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_equality(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_EQEQ, NIPPY_OPERATOR_EQ},
        {TOKEN_EQEQEQ, NIPPY_OPERATOR_STRICT_EQ},
        {TOKEN_NE, NIPPY_OPERATOR_NE}
    };

    return parse_binary_level(parse_relational, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_bit_and(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_AMP, NIPPY_OPERATOR_BIT_AND}
    };

    return parse_binary_level(parse_shift, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_bit_xor(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_CARET, NIPPY_OPERATOR_BIT_XOR}
    };

    return parse_binary_level(parse_bit_and, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_bit_or(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_PIPE, NIPPY_OPERATOR_BIT_OR}
    };

    return parse_binary_level(parse_bit_xor, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_logical_and(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_ANDAND, NIPPY_OPERATOR_AND}
    };

    return parse_binary_level(parse_equality, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_logical_or(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_OROR, NIPPY_OPERATOR_OR}
    };

    return parse_binary_level(parse_logical_and, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}


bool parse_expression(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    return parse_logical_or(tokens, string_bytes, ast, cursor, out_index, errors);
}
