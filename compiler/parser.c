#include "parser.h"

#include "messages.h"

#include <string.h>

static const Token *token_at(const TokenList *tokens, size_t index)
{
    return list_get(tokens, index);
}

/* Position to attach to a diagnostic when the offending token is missing. The
   token list always ends with EOF, so this is only a fallback for robustness. */
static SourcePos position_at(const TokenList *tokens, size_t index)
{
    const Token *token = token_at(tokens, index);
    SourcePos pos = {0, 1, 1};

    if (token != NULL) {
        pos = token->pos;
    }
    return pos;
}

static bool intern_token(Ast *ast, const Token *token, size_t *out_index,
                         ErrorState *errors)
{
    *out_index = ast_intern_symbol(ast, token->text, token->length);
    if (*out_index == AST_NO_NODE) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME);
        return false;
    }
    return true;
}

static bool push_node(Ast *ast, const AstNode *node, size_t *out_index)
{
    *out_index = ast->nodes.length;
    return list_push(&ast->nodes, node);
}

/* Push a top-level declaration node and record its index in the root list. */
static bool push_root(Ast *ast, const AstNode *node, ErrorState *errors)
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

static bool parse_expression(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
);
static bool parse_type(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
);
static bool parse_type_primary(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
);

/* TypeExpression ::= PrimaryType { '|' PrimaryType }

   Alternatives stay in source order here; flattening, deduplicating, and
   sorting belong to the checker. A single alternative is not wrapped in a
   union node. Nested runs are collected in a temporary list and appended only
   after parsing finishes, so every node's child run is contiguous. */
static bool parse_type(
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

/* PrimaryType ::= 'Fun' '<' '(' [ TypeExpression { ',' TypeExpression } ] ')'
                   ':' TypeExpression '>'
                | '(' TypeExpression ')'
                | IDENT [ '<' TypeExpression { ',' TypeExpression } '>' ]

   A function type is parsed recursively, so a parameter or return type may
   itself be a function type. Parameter type indices are appended to
   Ast.children, keeping variable-arity children out of the flat node array. */
static bool parse_type_primary(
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
            node.integer_value = length_token->integer_value;
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
                node.integer_value = capacity_token->integer_value;
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
                node.integer_value = capacity_token->integer_value;
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

/* TypeParameters ::= '<' IDENT { ',' IDENT } '>'
   Each parameter becomes an AST_TYPE_PARAMETER node. The run lives in
   Ast.type_parameters, which is disjoint from Ast.children, so nested type
   parsing cannot split it. */
static bool parse_type_parameters(
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

static bool push_literal(Ast *ast, const Token *token, size_t *out_index)
{
    AstNode node = {0};

    node.kind = AST_INTEGER_LITERAL;
    node.pos = token->pos;
    node.integer_value = token->integer_value;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;

    return push_node(ast, &node, out_index);
}

/* An argument is either a positional expression or a named wrapper. The
   wrapper is emitted after every argument has been parsed so the call's child
   run stays contiguous. */
typedef struct {
    size_t name_index;
    size_t value_index;
    SourcePos pos;
    bool named;
} Argument;

static bool argument_looks_like_expression(const Token *token)
{
    return token != NULL &&
           (token->type == TOKEN_INTEGER ||
            token->type == TOKEN_STRING ||
            token->type == TOKEN_IDENTIFIER ||
            token->type == TOKEN_KEYWORD_TRUE ||
            token->type == TOKEN_KEYWORD_FALSE ||
            token->type == TOKEN_KEYWORD_NIL ||
            token->type == TOKEN_BANG ||
            token->type == TOKEN_MINUS ||
            token->type == TOKEN_TILDE ||
            token->type == TOKEN_LPAREN ||
            token->type == TOKEN_LBRACKET ||
            token->type == TOKEN_LBRACE);
}

/* Call ::= IDENT '(' [ Argument { ',' Argument } ] ')'
   Argument ::= [ IDENT '=' ] Expression */
static bool parse_call(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *name_token = token_at(tokens, *cursor);
    size_t name_index;
    size_t children_start;
    bool named = false;
    bool positional = false;
    List arguments;
    AstNode node = {0};

    if (!intern_token(ast, name_token, &name_index, errors)) {
        return false;
    }
    *cursor += 2; /* past the name and '(' */

    list_init(&arguments, sizeof(Argument));

    {
        const Token *token = token_at(tokens, *cursor);

        if (token != NULL && token->type == TOKEN_RPAREN) {
            (*cursor)++;
        } else {
            for (;;) {
                Argument argument = {0};
                const Token *token = token_at(tokens, *cursor);

                argument.name_index = AST_NO_NODE;
                argument.pos = position_at(tokens, *cursor);

                if (token == NULL) {
                    error_report_at(errors, argument.pos,
                                    MESSAGE_PARSER_EXPECTED_ARGUMENT);
                    list_free(&arguments);
                    return false;
                }

                if (token->type == TOKEN_IDENTIFIER) {
                    const Token *peek = token_at(tokens, *cursor + 1);

                    if (peek != NULL && peek->type == TOKEN_EQUALS) {
                        if (positional) {
                            error_report_at(errors, token->pos,
                                            MESSAGE_PARSER_MIXED_ARGS);
                            list_free(&arguments);
                            return false;
                        }
                        named = true;
                        argument.named = true;
                        argument.pos = token->pos;
                        if (!intern_token(ast, token, &argument.name_index,
                                          errors)) {
                            list_free(&arguments);
                            return false;
                        }
                        *cursor += 2; /* past the name and '=' */
                    }
                }

                if (!argument.named) {
                    if (named) {
                        error_report_at(errors, token->pos,
                                        MESSAGE_PARSER_MIXED_ARGS);
                        list_free(&arguments);
                        return false;
                    }
                    positional = true;
                }

                token = token_at(tokens, *cursor);
                if (!argument_looks_like_expression(token)) {
                    error_report_at(errors, position_at(tokens, *cursor),
                                    MESSAGE_PARSER_EXPECTED_ARGUMENT);
                    list_free(&arguments);
                    return false;
                }
                if (!parse_expression(tokens, string_bytes, ast, cursor,
                                      &argument.value_index, errors)) {
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
    }

    /* Named arguments get wrappers; positional expressions are already nodes
       and are stored directly. Both are collected in Ast.children. Record the
       run only now: nested calls in the arguments have already appended their
       own runs. */
    children_start = ast->children.length;
    {
        size_t i;

        for (i = 0; i < arguments.length; i++) {
            const Argument *argument = list_get(&arguments, i);

            if (argument->named) {
                AstNode wrapper = {0};
                size_t wrapper_index;

                wrapper.kind = AST_NAMED_ARG;
                wrapper.pos = argument->pos;
                wrapper.name_index = argument->name_index;
                wrapper.left_child = argument->value_index;
                wrapper.right_child = AST_NO_NODE;
                if (!push_node(ast, &wrapper, &wrapper_index) ||
                    !ast_push_child(ast, wrapper_index)) {
                    error_report(errors,
                                 MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                    list_free(&arguments);
                    return false;
                }
            } else if (!ast_push_child(ast, argument->value_index)) {
                error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                list_free(&arguments);
                return false;
            }
        }
    }

    node.kind = AST_CALL;
    node.pos = name_token->pos;
    node.name_index = name_index;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = children_start;
    node.children_count = arguments.length;
    list_free(&arguments);
    return push_node(ast, &node, out_index);
}

static bool parse_statement(const TokenList *tokens, const List *string_bytes, Ast *ast,
                            size_t *cursor, size_t *out_index,
                            ErrorState *errors);

static bool push_bool_literal(Ast *ast, const Token *token, bool value,
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

static bool parse_record_literal(const TokenList *tokens,
                                 const List *string_bytes, Ast *ast,
                                 size_t *cursor, size_t *out_index,
                                 ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    List children;
    size_t children_start;
    AstNode node = {0};

    (*cursor)++; /* past '{' */
    list_init(&children, sizeof(size_t));
    token = token_at(tokens, *cursor);
    while (token != NULL && token->type != TOKEN_RBRACE) {
        AstNode key = {0};
        size_t key_index;
        size_t value_index;
        size_t i;

        if (token->type != TOKEN_STRING) {
            error_report_at(errors, token->pos,
                            MESSAGE_PARSER_EXPECTED_EXPRESSION);
            list_free(&children);
            return false;
        }
        key.kind = AST_STRING_LITERAL;
        key.pos = token->pos;
        key.name_index = AST_NO_NODE;
        key.left_child = AST_NO_NODE;
        key.right_child = AST_NO_NODE;
        key.children_start = 0;
        key.children_count = 0;
        key.string_offset = ast->bytes.length;
        key.string_length = token->string_length;
        for (i = 0; i < token->string_length; i++) {
            const unsigned char *byte = list_get(
                string_bytes, token->string_offset + i);

            if (byte == NULL || !ast_push_byte(ast, *byte)) {
                error_report(errors,
                             MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
                list_free(&children);
                return false;
            }
        }
        if (!push_node(ast, &key, &key_index)) {
            list_free(&children);
            return false;
        }
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_COLON) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_COLON);
            list_free(&children);
            return false;
        }
        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor, &value_index,
                              errors) ||
            !list_push(&children, &key_index) ||
            !list_push(&children, &value_index)) {
            list_free(&children);
            return false;
        }
        token = token_at(tokens, *cursor);
        if (token != NULL && token->type == TOKEN_COMMA) {
            (*cursor)++;
            token = token_at(tokens, *cursor);
            continue;
        }
        break;
    }
    if (token == NULL || token->type != TOKEN_RBRACE) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_RBRACE);
        list_free(&children);
        return false;
    }
    (*cursor)++;
    children_start = ast->children.length;
    for (size_t i = 0; i < children.length; i++) {
        size_t child = *(const size_t *)list_get(&children, i);

        if (!ast_push_child(ast, child)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&children);
            return false;
        }
    }
    node.kind = AST_RECORD_LITERAL;
    node.pos = pos;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = children_start;
    node.children_count = children.length;
    list_free(&children);
    return push_node(ast, &node, out_index);
}

static bool parse_type_test(const TokenList *tokens, const List *string_bytes,
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

static bool parse_block(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors);

static bool match_head_ok(const Ast *ast, size_t head)
{
    const AstNode *node = ast_get_node(ast, head);

    return node != NULL && node->kind == AST_IDENTIFIER;
}

static bool parse_match(const TokenList *tokens, const List *string_bytes,
                        Ast *ast, size_t *cursor, bool expression_form,
                        size_t *out_index, ErrorState *errors)
{
    SourcePos pos = position_at(tokens, *cursor);
    bool type_match = false;
    size_t head;
    size_t else_body = AST_NO_NODE;
    List children;
    size_t children_start;
    AstNode node = {0};

    (*cursor)++; /* past match */
    if (token_at(tokens, *cursor) != NULL &&
        token_at(tokens, *cursor)->type == TOKEN_KEYWORD_TYPEOF) {
        (*cursor)++;
        type_match = true;
    }
    if (!parse_expression(tokens, string_bytes, ast, cursor, &head, errors)) {
        return false;
    }
    if (!match_head_ok(ast, head)) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_MATCH_HEAD);
        return false;
    }
    if (token_at(tokens, *cursor) == NULL ||
        token_at(tokens, *cursor)->type != TOKEN_KEYWORD_WITH) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_WITH);
        return false;
    }
    (*cursor)++;
    list_init(&children, sizeof(size_t));
    for (;;) {
        size_t pattern;
        size_t body;
        const Token *token;

        if (type_match) {
            if (!parse_type(tokens, string_bytes, ast, cursor, &pattern,
                            errors)) {
                list_free(&children);
                return false;
            }
        } else if (!parse_expression(tokens, string_bytes, ast, cursor,
                                     &pattern, errors)) {
            list_free(&children);
            return false;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_KEYWORD_THEN) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_THEN);
            list_free(&children);
            return false;
        }
        (*cursor)++;
        if (expression_form) {
            if (!parse_expression(tokens, string_bytes, ast, cursor, &body,
                                  errors)) {
                list_free(&children);
                return false;
            }
        } else if (!parse_block(tokens, string_bytes, ast, cursor, &body,
                                errors)) {
            list_free(&children);
            return false;
        }
        if (!list_push(&children, &pattern) ||
            !list_push(&children, &body)) {
            list_free(&children);
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
        if (token_at(tokens, *cursor) != NULL &&
            token_at(tokens, *cursor)->type == TOKEN_KEYWORD_WITH) {
            (*cursor)++;
            continue;
        }
        break;
    }
    if (token_at(tokens, *cursor) != NULL &&
        token_at(tokens, *cursor)->type == TOKEN_KEYWORD_ELSE) {
        (*cursor)++;
        if (expression_form) {
            if (!parse_expression(tokens, string_bytes, ast, cursor,
                                  &else_body, errors)) {
                list_free(&children);
                return false;
            }
        } else if (!parse_block(tokens, string_bytes, ast, cursor, &else_body,
                                errors)) {
            list_free(&children);
            return false;
        }
    } else if (expression_form) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_ELSE);
        list_free(&children);
        return false;
    }
    if (token_at(tokens, *cursor) == NULL ||
        token_at(tokens, *cursor)->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        list_free(&children);
        return false;
    }
    (*cursor)++;
    children_start = ast->children.length;
    for (size_t i = 0; i < children.length; i++) {
        size_t child = *(const size_t *)list_get(&children, i);

        if (!ast_push_child(ast, child)) {
            list_free(&children);
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
    }
    list_free(&children);
    node.kind = expression_form ? AST_MATCH_EXPRESSION : AST_MATCH;
    node.pos = pos;
    node.integer_value = type_match ? 1 : 0;
    node.name_index = AST_NO_NODE;
    node.left_child = head;
    node.right_child = else_body;
    node.children_start = children_start;
    node.children_count = ast->children.length - children_start;
    return push_node(ast, &node, out_index);
}

static bool parse_if_expression(const TokenList *tokens,
                               const List *string_bytes, Ast *ast,
                               size_t *cursor, size_t *out_index,
                               ErrorState *errors)
{
    SourcePos pos = position_at(tokens, *cursor);
    List conditions;
    List expressions;
    size_t else_expression = AST_NO_NODE;
    size_t i;
    bool ok = false;

    (*cursor)++; /* past if */
    list_init(&conditions, sizeof(size_t));
    list_init(&expressions, sizeof(size_t));
    for (;;) {
        size_t condition;
        size_t expression;
        const Token *token;

        if (!parse_expression(tokens, string_bytes, ast, cursor, &condition,
                              errors) ||
            !list_push(&conditions, &condition)) {
            goto done;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_KEYWORD_THEN) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_THEN);
            goto done;
        }
        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor, &expression,
                              errors) ||
            !list_push(&expressions, &expression)) {
            goto done;
        }
        token = token_at(tokens, *cursor);
        if (token != NULL && token->type == TOKEN_KEYWORD_ELSEIF) {
            (*cursor)++;
            continue;
        }
        break;
    }
    {
        const Token *token = token_at(tokens, *cursor);

        if (token == NULL || token->type != TOKEN_KEYWORD_ELSE) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_ELSE);
            goto done;
        }
        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor,
                              &else_expression, errors)) {
            goto done;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_KEYWORD_END) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
            goto done;
        }
        (*cursor)++;
    }
    for (i = expressions.length; i > 0; i--) {
        AstNode node = {0};
        size_t condition = *(const size_t *)list_get(&conditions, i - 1);
        size_t expression = *(const size_t *)list_get(&expressions, i - 1);
        size_t children_start = ast->children.length;

        if (!ast_push_child(ast, else_expression)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            goto done;
        }
        node.kind = AST_IF_EXPRESSION;
        node.pos = pos;
        node.name_index = AST_NO_NODE;
        node.left_child = condition;
        node.right_child = expression;
        node.children_start = children_start;
        node.children_count = 1;
        if (!push_node(ast, &node, &else_expression)) {
            goto done;
        }
    }
    *out_index = else_expression;
    ok = true;

done:
    list_free(&conditions);
    list_free(&expressions);
    return ok;
}

static bool parse_primary(
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
    if (token->type == TOKEN_INTEGER) {
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

/* After `.name <`, decide whether the angle list is a member type argument
   followed by '(' or a comparison starting at a field access. */
static bool member_type_arguments_ahead(const TokenList *tokens, size_t cursor)
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

/* Postfix ::= Primary { '.' IDENT [ '<' TypeExpression '>' ]
                              '(' [ Expression { ',' Expression } ] ')' }

   Every member name is a built-in member call or field. Its optional type
   argument and positional arguments become links on the node. Argument nodes
   are appended only after all of them have been parsed, so nested expressions
   cannot split the run. */
static bool parse_postfix(
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

static bool parse_unary(
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

typedef bool (*ParseLevel)(const TokenList *tokens,
                           const List *string_bytes, Ast *ast,
                           size_t *cursor, size_t *out_index,
                           ErrorState *errors);

typedef struct {
    TokenType token;
    NippyOperator op;
} BinaryOperator;

static bool parse_binary_level(ParseLevel next, const BinaryOperator *operators,
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

static bool parse_multiplicative(
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

static bool parse_additive(
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

static bool parse_shift(
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

static bool parse_relational(
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

    return parse_binary_level(parse_shift, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}

static bool parse_equality(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_EQEQ, NIPPY_OPERATOR_EQ},
        {TOKEN_NE, NIPPY_OPERATOR_NE}
    };

    return parse_binary_level(parse_relational, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}

static bool parse_bit_and(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_AMP, NIPPY_OPERATOR_BIT_AND}
    };

    return parse_binary_level(parse_equality, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}

static bool parse_bit_xor(
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

static bool parse_bit_or(
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

static bool parse_logical_and(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    static const BinaryOperator operators[] = {
        {TOKEN_ANDAND, NIPPY_OPERATOR_AND}
    };

    return parse_binary_level(parse_bit_or, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              string_bytes, ast, cursor, out_index, errors);
}

static bool parse_logical_or(
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

static bool parse_expression(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    return parse_logical_or(tokens, string_bytes, ast, cursor, out_index, errors);
}

/* A block is a run of statements. It stops before 'elseif', 'else', or 'end';
   the enclosing construct consumes its own terminator. */
static bool parse_block(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    SourcePos pos = position_at(tokens, *cursor);
    List statements;
    size_t children_start;
    size_t i;
    AstNode block = {0};

    list_init(&statements, sizeof(size_t));
    for (;;) {
        const Token *token = token_at(tokens, *cursor);
        size_t statement_index;

        if (token == NULL) {
            error_report_at(errors, pos,
                            MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
            list_free(&statements);
            return false;
        }
        if (token->type == TOKEN_KEYWORD_END ||
            token->type == TOKEN_KEYWORD_ELSEIF ||
            token->type == TOKEN_KEYWORD_ELSE ||
            token->type == TOKEN_KEYWORD_WITH) {
            break;
        }
        if (!parse_statement(tokens, string_bytes, ast, cursor,
                             &statement_index, errors)) {
            list_free(&statements);
            return false;
        }
        if (!list_push(&statements, &statement_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&statements);
            return false;
        }
    }

    children_start = ast->children.length;
    for (i = 0; i < statements.length; i++) {
        size_t statement_index = *(const size_t *)list_get(&statements, i);

        if (!ast_push_child(ast, statement_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&statements);
            return false;
        }
    }
    block.kind = AST_BLOCK;
    block.pos = pos;
    block.left_child = AST_NO_NODE;
    block.right_child = AST_NO_NODE;
    block.children_start = children_start;
    block.children_count = statements.length;
    list_free(&statements);
    return push_node(ast, &block, out_index);
}

static bool wrap_in_block(Ast *ast, size_t statement_index, SourcePos pos,
                          size_t *out_index)
{
    size_t children_start = ast->children.length;
    AstNode block = {0};

    if (!ast_push_child(ast, statement_index)) {
        return false;
    }
    block.kind = AST_BLOCK;
    block.pos = pos;
    block.left_child = AST_NO_NODE;
    block.right_child = AST_NO_NODE;
    block.children_start = children_start;
    block.children_count = 1;
    return push_node(ast, &block, out_index);
}

static bool make_if_node(Ast *ast, size_t condition, size_t then_block,
                         size_t else_block, SourcePos pos, size_t *out_index)
{
    size_t children_start = ast->children.length;
    AstNode node = {0};

    if (!ast_push_child(ast, then_block)) {
        return false;
    }
    if (else_block != AST_NO_NODE && !ast_push_child(ast, else_block)) {
        return false;
    }
    node.kind = AST_IF;
    node.pos = pos;
    node.left_child = condition;
    node.right_child = AST_NO_NODE;
    node.children_start = children_start;
    node.children_count = else_block == AST_NO_NODE ? 1 : 2;
    return push_node(ast, &node, out_index);
}

static bool parse_if(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    size_t condition;
    size_t then_block;
    size_t else_block = AST_NO_NODE;
    size_t else_branch = AST_NO_NODE;
    List conditions;
    List blocks;
    size_t i;
    bool ok = false;

    (*cursor)++; /* past 'if' */
    if (!parse_expression(tokens, string_bytes, ast, cursor, &condition, errors)) {
        return false;
    }

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_THEN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_THEN);
        return false;
    }
    (*cursor)++;

    if (!parse_block(tokens, string_bytes, ast, cursor, &then_block, errors)) {
        return false;
    }

    list_init(&conditions, sizeof(size_t));
    list_init(&blocks, sizeof(size_t));
    if (!list_push(&conditions, &condition) ||
        !list_push(&blocks, &then_block)) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        goto done;
    }

    for (;;) {
        token = token_at(tokens, *cursor);

        if (token == NULL || token->type != TOKEN_KEYWORD_ELSEIF) {
            break;
        }
        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor, &condition, errors)) {
            goto done;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_KEYWORD_THEN) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_THEN);
            goto done;
        }
        (*cursor)++;
        if (!parse_block(tokens, string_bytes, ast, cursor, &then_block, errors)) {
            goto done;
        }
        if (!list_push(&conditions, &condition) ||
            !list_push(&blocks, &then_block)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            goto done;
        }
    }

    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_KEYWORD_ELSE) {
        (*cursor)++;
        if (!parse_block(tokens, string_bytes, ast, cursor, &else_block, errors)) {
            goto done;
        }
        else_branch = else_block;
    }

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        goto done;
    }
    (*cursor)++;

    /* Desugar elseif chains into nested ifs in the else branch, from the
       inside out. Each if appends its branch blocks only after the branch
       blocks have been parsed, keeping its child run contiguous. */
    for (i = blocks.length; i > 1; i--) {
        size_t nested_if;
        size_t wrapper;

        if (!make_if_node(ast, *(const size_t *)list_get(&conditions, i - 1),
                          *(const size_t *)list_get(&blocks, i - 1),
                          else_branch, pos, &nested_if)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            goto done;
        }
        if (!wrap_in_block(ast, nested_if, pos, &wrapper)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            goto done;
        }
        else_branch = wrapper;
    }

    if (!make_if_node(ast, *(const size_t *)list_get(&conditions, 0),
                      *(const size_t *)list_get(&blocks, 0), else_branch, pos,
                      out_index)) {
        error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        goto done;
    }
    ok = true;

done:
    list_free(&conditions);
    list_free(&blocks);
    return ok;
}

static bool parse_while(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    size_t condition;
    size_t body;
    AstNode node = {0};

    (*cursor)++; /* past 'while' */
    if (!parse_expression(tokens, string_bytes, ast, cursor, &condition, errors)) {
        return false;
    }
    if (!parse_block(tokens, string_bytes, ast, cursor, &body, errors)) {
        return false;
    }
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }
    (*cursor)++;

    node.kind = AST_WHILE;
    node.pos = pos;
    node.left_child = condition;
    node.right_child = body;
    node.name_index = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}

static bool parse_for(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    size_t first_name;
    size_t second_name = AST_NO_NODE;
    size_t iterable;
    size_t body;
    AstNode node = {0};

    (*cursor)++; /* past 'for' */
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }
    if (!intern_token(ast, token, &first_name, errors)) {
        return false;
    }
    (*cursor)++;
    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_COMMA) {
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
            return false;
        }
        if (!intern_token(ast, token, &second_name, errors)) {
            return false;
        }
        (*cursor)++;
        token = token_at(tokens, *cursor);
    }
    if (token == NULL || token->type != TOKEN_KEYWORD_IN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_IN);
        return false;
    }
    (*cursor)++;
    if (!parse_expression(tokens, string_bytes, ast, cursor, &iterable,
                          errors)) {
        return false;
    }
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_DO) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_DO);
        return false;
    }
    (*cursor)++;
    if (!parse_block(tokens, string_bytes, ast, cursor, &body, errors)) {
        return false;
    }
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }
    (*cursor)++;

    node.kind = AST_FOR;
    node.pos = pos;
    node.name_index = first_name;
    node.second_name_index = second_name;
    node.left_child = iterable;
    node.right_child = body;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}

static bool assignment_ahead(const TokenList *tokens, size_t cursor)
{
    const Token *name = token_at(tokens, cursor);
    const Token *equals = token_at(tokens, cursor + 1);

    return name != NULL && name->type == TOKEN_IDENTIFIER &&
           equals != NULL && equals->type == TOKEN_EQUALS;
}

/* Recognize `name[...] =` without consuming it. Nested brackets are balanced;
   only the assignment form after the closing bracket matters here. */
static bool index_assignment_ahead(const TokenList *tokens, size_t cursor)
{
    const Token *name = token_at(tokens, cursor);
    size_t i;
    int depth = 0;

    if (name == NULL || name->type != TOKEN_IDENTIFIER) {
        return false;
    }
    i = cursor + 1;
    if (token_at(tokens, i) == NULL ||
        token_at(tokens, i)->type != TOKEN_LBRACKET) {
        return false;
    }
    for (; token_at(tokens, i) != NULL; i++) {
        TokenType type = token_at(tokens, i)->type;

        if (type == TOKEN_LBRACKET) {
            depth++;
        } else if (type == TOKEN_RBRACKET) {
            depth--;
            if (depth == 0) {
                const Token *equals = token_at(tokens, i + 1);

                return equals != NULL && equals->type == TOKEN_EQUALS;
            }
        } else if (type == TOKEN_EOF) {
            return false;
        }
    }
    return false;
}

/* Assignment ::= IDENT '=' Expression */
static bool parse_assignment(const TokenList *tokens,
                             const List *string_bytes, Ast *ast,
                             size_t *cursor, size_t *out_index,
                             ErrorState *errors)
{
    const Token *name_token = token_at(tokens, *cursor);
    size_t name_index;
    size_t value;
    AstNode node = {0};

    if (!intern_token(ast, name_token, &name_index, errors)) {
        return false;
    }
    *cursor += 2; /* past the name and '=' */
    if (!parse_expression(tokens, string_bytes, ast, cursor, &value,
                          errors)) {
        return false;
    }
    node.kind = AST_ASSIGNMENT;
    node.pos = name_token->pos;
    node.name_index = name_index;
    node.left_child = value;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}

/* IndexAssignment ::= IDENT '[' Expression ']' '=' Expression
   The target is the AST_INDEX expression parsed by parse_postfix. */
static bool parse_index_assignment(const TokenList *tokens,
                                   const List *string_bytes, Ast *ast,
                                   size_t *cursor, size_t *out_index,
                                   ErrorState *errors)
{
    SourcePos pos = position_at(tokens, *cursor);
    size_t target;
    size_t value;
    AstNode node = {0};
    const AstNode *target_node;

    if (!parse_postfix(tokens, string_bytes, ast, cursor, &target, errors)) {
        return false;
    }
    target_node = ast_get_node(ast, target);
    if (target_node == NULL || target_node->kind != AST_INDEX) {
        error_report_at(errors, pos, MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }
    if (token_at(tokens, *cursor) == NULL ||
        token_at(tokens, *cursor)->type != TOKEN_EQUALS) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_EQUALS);
        return false;
    }
    (*cursor)++;
    if (!parse_expression(tokens, string_bytes, ast, cursor, &value, errors)) {
        return false;
    }
    node.kind = AST_INDEX_ASSIGNMENT;
    node.pos = pos;
    node.name_index = AST_NO_NODE;
    node.left_child = target;
    node.right_child = value;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}

/* Recognize `name.field(.field)* =` at the current cursor. */
static bool field_assignment_ahead(const TokenList *tokens, size_t cursor)
{
    const Token *name = token_at(tokens, cursor);
    size_t i;

    if (name == NULL || name->type != TOKEN_IDENTIFIER) {
        return false;
    }
    i = cursor + 1;
    if (token_at(tokens, i) == NULL ||
        token_at(tokens, i)->type != TOKEN_DOT) {
        return false;
    }
    for (;;) {
        if (token_at(tokens, i) == NULL ||
            token_at(tokens, i)->type != TOKEN_DOT) {
            return false;
        }
        i++;
        if (token_at(tokens, i) == NULL ||
            token_at(tokens, i)->type != TOKEN_IDENTIFIER) {
            return false;
        }
        i++;
        if (token_at(tokens, i) != NULL &&
            token_at(tokens, i)->type == TOKEN_EQUALS) {
            return true;
        }
    }
}

static bool parse_field_assignment(const TokenList *tokens,
                                   const List *string_bytes, Ast *ast,
                                   size_t *cursor, size_t *out_index,
                                   ErrorState *errors)
{
    SourcePos pos = position_at(tokens, *cursor);
    size_t target;
    size_t value;
    AstNode node = {0};
    const AstNode *target_node;

    if (!parse_postfix(tokens, string_bytes, ast, cursor, &target, errors)) {
        return false;
    }
    target_node = ast_get_node(ast, target);
    if (target_node == NULL || target_node->kind != AST_MEMBER_ACCESS) {
        error_report_at(errors, pos, MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    if (token_at(tokens, *cursor) == NULL ||
        token_at(tokens, *cursor)->type != TOKEN_EQUALS) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_EQUALS);
        return false;
    }
    (*cursor)++;
    if (!parse_expression(tokens, string_bytes, ast, cursor, &value, errors)) {
        return false;
    }
    node.kind = AST_MEMBER_ASSIGNMENT;
    node.pos = pos;
    node.name_index = AST_NO_NODE;
    node.left_child = target;
    node.right_child = value;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}

/* ExprStmt ::= Expression
   Any expression parses as a statement; the checker decides whether it is a
   valid statement, so a bare literal is parsed here and rejected later. */
static bool parse_expression_statement(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    SourcePos pos = position_at(tokens, *cursor);
    size_t expression;
    AstNode node = {0};

    if (!parse_expression(tokens, string_bytes, ast, cursor, &expression, errors)) {
        return false;
    }
    node.kind = AST_EXPRESSION_STATEMENT;
    node.pos = pos;
    node.name_index = AST_NO_NODE;
    node.left_child = expression;
    node.right_child = AST_NO_NODE;
    return push_node(ast, &node, out_index);
}

static bool parse_var_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, bool is_global, bool is_const,
    bool as_statement, size_t *out_index, ErrorState *errors);

static bool parse_statement(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};

    if (token == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }

    if (token->type == TOKEN_KEYWORD_VAR ||
        token->type == TOKEN_KEYWORD_CONST) {
        bool is_const = token->type == TOKEN_KEYWORD_CONST;

        (*cursor)++;
        return parse_var_declaration(tokens, string_bytes, ast, cursor,
                                     token->pos, false, is_const, true,
                                     out_index, errors);
    }

    if (token->type == TOKEN_KEYWORD_RETURN) {
        size_t expression;

        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor, &expression, errors)) {
            return false;
        }
        node.kind = AST_RETURN;
        node.pos = token->pos;
        node.left_child = expression;
        node.right_child = AST_NO_NODE;
        node.name_index = AST_NO_NODE;
        node.children_start = 0;
        node.children_count = 0;
        return push_node(ast, &node, out_index);
    }
    if (token->type == TOKEN_KEYWORD_IF) {
        return parse_if(tokens, string_bytes, ast, cursor, out_index, errors);
    }
    if (token->type == TOKEN_KEYWORD_MATCH) {
        return parse_match(tokens, string_bytes, ast, cursor, false,
                           out_index, errors);
    }
    if (token->type == TOKEN_KEYWORD_WHILE) {
        return parse_while(tokens, string_bytes, ast, cursor, out_index, errors);
    }
    if (token->type == TOKEN_KEYWORD_FOR) {
        return parse_for(tokens, string_bytes, ast, cursor, out_index, errors);
    }
    if (token->type == TOKEN_KEYWORD_BREAK ||
        token->type == TOKEN_KEYWORD_CONTINUE) {
        (*cursor)++;
        node.kind = token->type == TOKEN_KEYWORD_BREAK ? AST_BREAK
                                                       : AST_CONTINUE;
        node.pos = token->pos;
        node.left_child = AST_NO_NODE;
        node.right_child = AST_NO_NODE;
        node.name_index = AST_NO_NODE;
        node.children_start = 0;
        node.children_count = 0;
        return push_node(ast, &node, out_index);
    }
    if (assignment_ahead(tokens, *cursor)) {
        return parse_assignment(tokens, string_bytes, ast, cursor, out_index,
                                errors);
    }
    if (index_assignment_ahead(tokens, *cursor)) {
        return parse_index_assignment(tokens, string_bytes, ast, cursor,
                                      out_index, errors);
    }
    if (field_assignment_ahead(tokens, *cursor)) {
        return parse_field_assignment(tokens, string_bytes, ast, cursor,
                                      out_index, errors);
    }
    if (argument_looks_like_expression(token)) {
        return parse_expression_statement(tokens, string_bytes, ast, cursor,
                                          out_index, errors);
    }

    error_report_at(errors, token->pos,
                    MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
    return false;
}

/* VarDecl ::= ('var' | 'global') IDENT (':' Type)? '=' Expression.
   The leading keyword has already been consumed; `*cursor` points at the
   variable name. `global` is restricted to Store declarations by the
   checker. */
static bool parse_var_declaration(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    SourcePos declaration_pos, bool is_global, bool is_const,
    bool as_statement, size_t *out_index, ErrorState *errors
)
{
    const Token *token;
    size_t name_index;
    size_t type_index = AST_NO_NODE;
    size_t initialiser_index;
    AstNode declaration = {0};

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
        return false;
    }
    if (!intern_token(ast, token, &name_index, errors)) {
        return false;
    }
    (*cursor)++;

    token = token_at(tokens, *cursor);
    if (token != NULL && token->type == TOKEN_COLON) {
        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_TYPE_NAME);
            return false;
        }
        if (!parse_type(tokens, string_bytes, ast, cursor, &type_index, errors)) {
            return false;
        }
    }

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_EQUALS) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_EQUALS);
        return false;
    }
    (*cursor)++;

    if (!parse_expression(tokens, string_bytes, ast, cursor,
                          &initialiser_index, errors)) {
        return false;
    }

    declaration.kind = AST_DECLARATION;
    declaration.pos = declaration_pos;
    declaration.name_index = name_index;
    declaration.left_child = initialiser_index;
    declaration.right_child = type_index;
    declaration.is_global = is_global;
    declaration.is_const = is_const;
    if (as_statement) {
        return push_node(ast, &declaration, out_index);
    }
    return push_root(ast, &declaration, errors);
}

/* TypeDecl ::= 'type' IDENT [ '<' TypeParameter { ',' TypeParameter } '>' ]
                ( '=' TypeExpression | Field { Field } 'end' )
   Field ::= IDENT ':' TypeExpression.
   The leading 'type' keyword has already been consumed; `*cursor` points at
   the type name. */
static bool parse_type_declaration(
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

/* FunDecl ::= 'fun' IDENT '(' [ Param { ',' Param } ] ')' ':' Type
               'return' Expression 'end'
   Param ::= IDENT ':' Type. The leading 'fun' keyword has already been
   consumed; `*cursor` points at the function name. */
static bool parse_function_declaration(
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

/* Program ::= (VarDecl | GlobalDecl | TypeDecl | FunDecl | Assignment | ExprStmt)+ EOF
   An assignment or expression statement becomes a root so it can sit between
   declarations; the checker decides whether an expression is call-shaped. */
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
