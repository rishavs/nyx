#include "parser_internal.h"

bool parse_type(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    size_t start = *cursor;
    size_t first;
    size_t children_start;
    size_t i;
    List alternatives;
    AstNode node = {0};

    if (!parse_type_primary(tokens, string_bytes, ast, cursor, &first, errors)) {
        return false;
    }

    list_init(&alternatives, sizeof(size_t));
    if (!list_push(&alternatives, &first)) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        list_free(&alternatives);
        return false;
    }

    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        size_t alternative;

        if (token == NULL || token->type != TOKEN_PIPE) {
            break;
        }
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token == NULL ||
            (token->type != TOKEN_IDENTIFIER && token->type != TOKEN_LPAREN)) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_UNION_ALTERNATIVE);
            list_free(&alternatives);
            return false;
        }
        if (!parse_type_primary(tokens, string_bytes, ast, cursor,
                               &alternative, errors)) {
            list_free(&alternatives);
            return false;
        }
        if (!list_push(&alternatives, &alternative)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&alternatives);
            return false;
        }
    }

    if (alternatives.length == 1) {
        *out_index = first;
        list_free(&alternatives);
        return true;
    }

    children_start = ast->children.length;
    for (i = 0; i < alternatives.length; i++) {
        size_t alternative = *(const size_t *)list_get(&alternatives, i);

        if (!ast_push_child(ast, alternative)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&alternatives);
            return false;
        }
    }
    node.kind = AST_TYPE_UNION;
    node.pos = position_at(tokens, start);
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = children_start;
    node.children_count = alternatives.length;
    list_free(&alternatives);
    return push_node(ast, &node, out_index);
}

bool parse_type_primary(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};
    size_t name_index;
    SourcePos pos;

    if (token == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_TYPE);
        return false;
    }

    if (token->type == TOKEN_LPAREN) {
        size_t inner;

        (*cursor)++;
        if (!parse_type(tokens, string_bytes, ast, cursor, &inner, errors)) {
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

    if (token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, token->pos, MESSAGE_PARSER_EXPECTED_TYPE);
        return false;
    }
    pos = token->pos;

    if (token->length == 6 && memcmp(token->text, "String", 6) == 0) {
        const Token *next = token_at(tokens, *cursor + 1);

        if (next != NULL && next->type == TOKEN_LT) {
            const Token *length_token;

            *cursor += 2; /* past String and '<' */
            length_token = token_at(tokens, *cursor);
            if (length_token == NULL ||
                length_token->type != TOKEN_INTEGER) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_STRING_LENGTH);
                return false;
            }
            node.kind = AST_TYPE_FIXED_STRING;
            node.pos = pos;
            node.integer_value = length_token->integer_value_u > (uint64_t)INT_MAX
                                   ? -1
                                   : (int)length_token->integer_value_u;
            node.name_index = AST_NO_NODE;
            node.left_child = AST_NO_NODE;
            node.right_child = AST_NO_NODE;
            node.children_start = 0;
            node.children_count = 0;
            (*cursor)++;
            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_GT) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_STRING_GT);
                return false;
            }
            (*cursor)++;
            return push_node(ast, &node, out_index);
        }
    }

    if ((token->length == 4 && memcmp(token->text, "List", 4) == 0) ||
        (token->length == 5 && memcmp(token->text, "Array", 5) == 0)) {
        bool is_array = token->length == 5;
        const Token *next = token_at(tokens, *cursor + 1);

        if (next != NULL && next->type == TOKEN_LT) {
            size_t element_type;
            const Token *separator;

            *cursor += 2; /* past the collection name and '<' */
            if (!parse_type(tokens, string_bytes, ast, cursor, &element_type,
                            errors)) {
                return false;
            }
            separator = token_at(tokens, *cursor);
            if (separator == NULL) {
                error_report_at(errors, position_at(tokens, *cursor),
                                is_array ? MESSAGE_PARSER_EXPECTED_ARRAY_GT
                                         : MESSAGE_PARSER_EXPECTED_LIST_GT);
                return false;
            }
            if (separator->type == TOKEN_COMMA) {
                const Token *capacity_token;

                if (!is_array) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_LIST_CAPACITY);
                    return false;
                }
                (*cursor)++;
                capacity_token = token_at(tokens, *cursor);
                if (capacity_token == NULL ||
                    capacity_token->type != TOKEN_INTEGER) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY);
                    return false;
                }
                node.kind = AST_TYPE_FIXED_LIST;
                node.pos = pos;
                node.integer_value = capacity_token->integer_value_u > (uint64_t)INT_MAX
                                       ? -1
                                       : (int)capacity_token->integer_value_u;
                node.name_index = AST_NO_NODE;
                node.left_child = element_type;
                node.right_child = AST_NO_NODE;
                node.children_start = 0;
                node.children_count = 0;
                (*cursor)++;
                separator = token_at(tokens, *cursor);
                if (separator == NULL || separator->type != TOKEN_GT) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_ARRAY_GT);
                    return false;
                }
                (*cursor)++;
                return push_node(ast, &node, out_index);
            }
            if (separator->type != TOKEN_GT) {
                error_report_at(errors, position_at(tokens, *cursor),
                                is_array ? MESSAGE_PARSER_EXPECTED_ARRAY_GT
                                         : MESSAGE_PARSER_EXPECTED_LIST_GT);
                return false;
            }
            if (is_array) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY);
                return false;
            }
            node.kind = AST_TYPE_LIST;
            node.pos = pos;
            node.name_index = AST_NO_NODE;
            node.left_child = element_type;
            node.right_child = AST_NO_NODE;
            node.children_start = 0;
            node.children_count = 0;
            (*cursor)++;
            return push_node(ast, &node, out_index);
        }
    }

    if ((token->length == 6 && memcmp(token->text, "Record", 6) == 0) ||
        (token->length == 5 && memcmp(token->text, "Table", 5) == 0)) {
        bool is_record = token->length == 6;
        const Token *next = token_at(tokens, *cursor + 1);

        if (next != NULL && next->type == TOKEN_LT) {
            size_t key_type;
            size_t value_type;
            const Token *separator;

            *cursor += 2;
            if (!parse_type(tokens, string_bytes, ast, cursor, &key_type,
                            errors)) {
                return false;
            }
            separator = token_at(tokens, *cursor);
            if (separator == NULL || separator->type != TOKEN_COMMA) {
                error_report_at(errors, position_at(tokens, *cursor),
                                is_record ? MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY
                                          : MESSAGE_PARSER_EXPECTED_TABLE_GT);
                return false;
            }
            (*cursor)++;
            if (!parse_type(tokens, string_bytes, ast, cursor, &value_type,
                            errors)) {
                return false;
            }
            separator = token_at(tokens, *cursor);
            node.kind = is_record ? AST_TYPE_RECORD : AST_TYPE_TABLE;
            node.pos = pos;
            node.name_index = AST_NO_NODE;
            node.left_child = key_type;
            node.right_child = value_type;
            node.children_start = 0;
            node.children_count = 0;
            if (is_record) {
                const Token *capacity_token;

                if (separator == NULL || separator->type != TOKEN_COMMA) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY);
                    return false;
                }
                (*cursor)++;
                capacity_token = token_at(tokens, *cursor);
                if (capacity_token == NULL ||
                    capacity_token->type != TOKEN_INTEGER) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY);
                    return false;
                }
                node.integer_value = capacity_token->integer_value_u > (uint64_t)INT_MAX
                                       ? -1
                                       : (int)capacity_token->integer_value_u;
                (*cursor)++;
                separator = token_at(tokens, *cursor);
                if (separator == NULL || separator->type != TOKEN_GT) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_RECORD_GT);
                    return false;
                }
            } else if (separator == NULL || separator->type != TOKEN_GT) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_TABLE_GT);
                return false;
            }
            (*cursor)++;
            return push_node(ast, &node, out_index);
        }
    }

    if (token->length == 5 && memcmp(token->text, "Slice", 5) == 0) {
        const Token *next = token_at(tokens, *cursor + 1);

        if (next != NULL && next->type == TOKEN_LT) {
            size_t element_type;

            *cursor += 2; /* past Slice and '<' */
            if (!parse_type(tokens, string_bytes, ast, cursor, &element_type,
                            errors)) {
                return false;
            }
            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_GT) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT);
                return false;
            }
            (*cursor)++;
            node.kind = AST_TYPE_SLICE;
            node.pos = pos;
            node.name_index = AST_NO_NODE;
            node.left_child = element_type;
            node.right_child = AST_NO_NODE;
            node.children_start = 0;
            node.children_count = 0;
            return push_node(ast, &node, out_index);
        }
    }

    if (token->length == 3 && token->text[0] == 'F' && token->text[1] == 'u' &&
        token->text[2] == 'n') {
        const Token *next = token_at(tokens, *cursor + 1);

        if (next != NULL && next->type == TOKEN_LT) {
            size_t children_start;
            size_t return_type;
            size_t i;
            SourcePos pos = token->pos;
            List parameter_types;

            *cursor += 2; /* past 'Fun' and '<' */

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_LPAREN) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN);
                return false;
            }
            (*cursor)++;

            list_init(&parameter_types, sizeof(size_t));
            token = token_at(tokens, *cursor);
            if (token != NULL && token->type == TOKEN_RPAREN) {
                (*cursor)++;
            } else {
                for (;;) {
                    size_t parameter_type;

                    if (!parse_type(tokens, string_bytes, ast, cursor, &parameter_type,
                                    errors)) {
                        list_free(&parameter_types);
                        return false;
                    }
                    if (!list_push(&parameter_types, &parameter_type)) {
                        error_report(errors,
                                     MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                        list_free(&parameter_types);
                        return false;
                    }

                    token = token_at(tokens, *cursor);
                    if (token == NULL) {
                        error_report_at(
                            errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR);
                        list_free(&parameter_types);
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
                    error_report_at(
                        errors, token->pos,
                        MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR);
                    list_free(&parameter_types);
                    return false;
                }
            }

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_COLON) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_COLON);
                list_free(&parameter_types);
                return false;
            }
            (*cursor)++;

            if (!parse_type(tokens, string_bytes, ast, cursor, &return_type, errors)) {
                list_free(&parameter_types);
                return false;
            }

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type != TOKEN_GT) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT);
                list_free(&parameter_types);
                return false;
            }
            (*cursor)++;

            /* Append the parameter run only after every nested type has been
               parsed, so this node's children stay contiguous. */
            children_start = ast->children.length;
            for (i = 0; i < parameter_types.length; i++) {
                size_t parameter_type = *(const size_t *)list_get(
                    &parameter_types, i);

                if (!ast_push_child(ast, parameter_type)) {
                    error_report(errors,
                                 MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                    list_free(&parameter_types);
                    return false;
                }
            }
            node.children_count = parameter_types.length;
            list_free(&parameter_types);

            node.kind = AST_TYPE_FUNCTION;
            node.pos = pos;
            node.name_index = AST_NO_NODE;
            node.left_child = AST_NO_NODE;
            node.right_child = return_type;
            node.children_start = children_start;
            return push_node(ast, &node, out_index);
        }
    }

    if (!intern_token(ast, token, &name_index, errors)) {
        return false;
    }
    (*cursor)++;

    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_LT) {
        size_t children_start;
        size_t i;
        List arguments;

        (*cursor)++;
        list_init(&arguments, sizeof(size_t));
        for (;;) {
            size_t argument;

            token = token_at(tokens, *cursor);
            if (token == NULL ||
                (token->type != TOKEN_IDENTIFIER &&
                 token->type != TOKEN_LPAREN)) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT);
                list_free(&arguments);
                return false;
            }
            if (!parse_type(tokens, string_bytes, ast, cursor, &argument, errors)) {
                list_free(&arguments);
                return false;
            }
            if (!list_push(&arguments, &argument)) {
                error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                list_free(&arguments);
                return false;
            }

            token = token_at(tokens, *cursor);
            if (token == NULL || token->type == TOKEN_EOF) {
                error_report_at(errors, position_at(tokens, *cursor),
                                MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT);
                list_free(&arguments);
                return false;
            }
            if (token->type == TOKEN_COMMA) {
                (*cursor)++;
                continue;
            }
            if (token->type == TOKEN_GT) {
                (*cursor)++;
                break;
            }
            error_report_at(errors, token->pos,
                            MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_SEPARATOR);
            list_free(&arguments);
            return false;
        }

        children_start = ast->children.length;
        for (i = 0; i < arguments.length; i++) {
            size_t argument = *(const size_t *)list_get(&arguments, i);

            if (!ast_push_child(ast, argument)) {
                error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                list_free(&arguments);
                return false;
            }
        }
        node.kind = AST_TYPE_APPLICATION;
        node.pos = pos;
        node.name_index = name_index;
        node.left_child = AST_NO_NODE;
        node.right_child = AST_NO_NODE;
        node.children_start = children_start;
        node.children_count = arguments.length;
        list_free(&arguments);
        return push_node(ast, &node, out_index);
    }

    node.kind = AST_TYPE_NAME;
    node.pos = pos;
    node.name_index = name_index;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}

bool parse_type_parameters(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_start, size_t *out_count, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    size_t start;

    (void)string_bytes;

    *out_start = 0;
    *out_count = 0;
    if (token == NULL || token->type != TOKEN_LT) {
        return true;
    }
    (*cursor)++;
    start = ast->type_parameters.length;
    for (;;) {
        AstNode parameter = {0};
        size_t index;

        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_NAME);
            return false;
        }
        parameter.kind = AST_TYPE_PARAMETER;
        parameter.pos = token->pos;
        parameter.left_child = AST_NO_NODE;
        parameter.right_child = AST_NO_NODE;
        if (!intern_token(ast, token, &parameter.name_index, errors)) {
            return false;
        }
        if (!push_node(ast, &parameter, &index) ||
            !ast_push_type_parameter(ast, index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
        (*cursor)++;

        token = token_at(tokens, *cursor);
        if (token == NULL || token->type == TOKEN_EOF) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_GT);
            return false;
        }
        if (token->type == TOKEN_COMMA) {
            (*cursor)++;
            continue;
        }
        if (token->type == TOKEN_GT) {
            (*cursor)++;
            break;
        }
        error_report_at(errors, token->pos,
                        MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_SEPARATOR);
        return false;
    }
    *out_start = start;
    *out_count = ast->type_parameters.length - start;
    return true;
}
