#include "parser.h"

#include "messages.h"

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

static bool parse_expression(const TokenList *tokens, Ast *ast, size_t *cursor,
                             size_t *out_index, ErrorState *errors);

/* Type ::= IDENT | 'Fun' '<' '(' [ Type { ',' Type } ] ')' ':' Type '>'

   A function type is parsed recursively, so a parameter or return type may
   itself be a function type. Its parameter type indices are appended to
   Ast.children, keeping variable-arity children out of the flat node array. */
static bool parse_type(const TokenList *tokens, Ast *ast, size_t *cursor,
                       size_t *out_index, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};

    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_TYPE);
        return false;
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

                    if (!parse_type(tokens, ast, cursor, &parameter_type,
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

            if (!parse_type(tokens, ast, cursor, &return_type, errors)) {
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

    node.kind = AST_TYPE_NAME;
    node.pos = token->pos;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    if (!intern_token(ast, token, &node.name_index, errors)) {
        return false;
    }
    (*cursor)++;
    return push_node(ast, &node, out_index);
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
            token->type == TOKEN_IDENTIFIER ||
            token->type == TOKEN_KEYWORD_TRUE ||
            token->type == TOKEN_KEYWORD_FALSE ||
            token->type == TOKEN_BANG ||
            token->type == TOKEN_LPAREN);
}

/* Call ::= IDENT '(' [ Argument { ',' Argument } ] ')'
   Argument ::= [ IDENT '=' ] Expression */
static bool parse_call(const TokenList *tokens, Ast *ast, size_t *cursor,
                       size_t *out_index, ErrorState *errors)
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
                if (!parse_expression(tokens, ast, cursor,
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

static bool parse_statement(const TokenList *tokens, Ast *ast,
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

static bool parse_primary(const TokenList *tokens, Ast *ast, size_t *cursor,
                          size_t *out_index, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};

    if (token == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_EXPRESSION);
        return false;
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
        if (!parse_expression(tokens, ast, cursor, &inner, errors)) {
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
    if (token->type == TOKEN_IDENTIFIER) {
        const Token *next = token_at(tokens, *cursor + 1);

        if (next != NULL && next->type == TOKEN_LPAREN) {
            return parse_call(tokens, ast, cursor, out_index, errors);
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

static bool parse_unary(const TokenList *tokens, Ast *ast, size_t *cursor,
                        size_t *out_index, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};
    size_t operand;

    if (token != NULL && token->type == TOKEN_BANG) {
        (*cursor)++;
        if (!parse_unary(tokens, ast, cursor, &operand, errors)) {
            return false;
        }
        node.kind = AST_UNARY;
        node.pos = token->pos;
        node.op = NIPPY_OPERATOR_NOT;
        node.name_index = AST_NO_NODE;
        node.left_child = operand;
        node.right_child = AST_NO_NODE;
        return push_node(ast, &node, out_index);
    }
    return parse_primary(tokens, ast, cursor, out_index, errors);
}

static bool parse_additive(const TokenList *tokens, Ast *ast, size_t *cursor,
                           size_t *out_index, ErrorState *errors);
static bool parse_unary(const TokenList *tokens, Ast *ast, size_t *cursor,
                        size_t *out_index, ErrorState *errors);

typedef bool (*ParseLevel)(const TokenList *tokens, Ast *ast, size_t *cursor,
                           size_t *out_index, ErrorState *errors);

typedef struct {
    TokenType token;
    NippyOperator op;
} BinaryOperator;

static bool parse_binary_level(ParseLevel next, const BinaryOperator *operators,
                               size_t operator_count, const TokenList *tokens,
                               Ast *ast, size_t *cursor, size_t *out_index,
                               ErrorState *errors)
{
    size_t left;

    if (!next(tokens, ast, cursor, &left, errors)) {
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
        if (!next(tokens, ast, cursor, &right, errors)) {
            return false;
        }
        node.kind = AST_BINARY;
        node.pos = token->pos;
        node.op = matched->op;
        node.name_index = AST_NO_NODE;
        node.left_child = left;
        node.right_child = right;
        if (!push_node(ast, &node, &left)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
    }

    *out_index = left;
    return true;
}

static bool parse_relational(const TokenList *tokens, Ast *ast, size_t *cursor,
                             size_t *out_index, ErrorState *errors)
{
    static const BinaryOperator operators[] = {
        {TOKEN_LT, NIPPY_OPERATOR_LT},
        {TOKEN_LE, NIPPY_OPERATOR_LE},
        {TOKEN_GT, NIPPY_OPERATOR_GT},
        {TOKEN_GE, NIPPY_OPERATOR_GE}
    };

    return parse_binary_level(parse_additive, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              ast, cursor, out_index, errors);
}

static bool parse_additive(const TokenList *tokens, Ast *ast, size_t *cursor,
                           size_t *out_index, ErrorState *errors)
{
    static const BinaryOperator operators[] = {
        {TOKEN_PLUS, NIPPY_OPERATOR_ADD}
    };

    return parse_binary_level(parse_unary, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              ast, cursor, out_index, errors);
}

static bool parse_equality(const TokenList *tokens, Ast *ast, size_t *cursor,
                           size_t *out_index, ErrorState *errors)
{
    static const BinaryOperator operators[] = {
        {TOKEN_EQEQ, NIPPY_OPERATOR_EQ},
        {TOKEN_NE, NIPPY_OPERATOR_NE}
    };

    return parse_binary_level(parse_relational, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              ast, cursor, out_index, errors);
}

static bool parse_logical_and(const TokenList *tokens, Ast *ast, size_t *cursor,
                              size_t *out_index, ErrorState *errors)
{
    static const BinaryOperator operators[] = {
        {TOKEN_ANDAND, NIPPY_OPERATOR_AND}
    };

    return parse_binary_level(parse_equality, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              ast, cursor, out_index, errors);
}

static bool parse_logical_or(const TokenList *tokens, Ast *ast, size_t *cursor,
                             size_t *out_index, ErrorState *errors)
{
    static const BinaryOperator operators[] = {
        {TOKEN_OROR, NIPPY_OPERATOR_OR}
    };

    return parse_binary_level(parse_logical_and, operators,
                              sizeof(operators) / sizeof(operators[0]), tokens,
                              ast, cursor, out_index, errors);
}

static bool parse_expression(const TokenList *tokens, Ast *ast, size_t *cursor,
                             size_t *out_index, ErrorState *errors)
{
    return parse_logical_or(tokens, ast, cursor, out_index, errors);
}

/* A block is a run of statements. It stops before 'elseif', 'else', or 'end';
   the enclosing construct consumes its own terminator. */
static bool parse_block(const TokenList *tokens, Ast *ast, size_t *cursor,
                        size_t *out_index, ErrorState *errors)
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
            token->type == TOKEN_KEYWORD_ELSE) {
            break;
        }
        if (!parse_statement(tokens, ast, cursor, &statement_index, errors)) {
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

static bool parse_if(const TokenList *tokens, Ast *ast, size_t *cursor,
                     size_t *out_index, ErrorState *errors)
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
    if (!parse_expression(tokens, ast, cursor, &condition, errors)) {
        return false;
    }

    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_THEN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_THEN);
        return false;
    }
    (*cursor)++;

    if (!parse_block(tokens, ast, cursor, &then_block, errors)) {
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
        if (!parse_expression(tokens, ast, cursor, &condition, errors)) {
            goto done;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_KEYWORD_THEN) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_THEN);
            goto done;
        }
        (*cursor)++;
        if (!parse_block(tokens, ast, cursor, &then_block, errors)) {
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
        if (!parse_block(tokens, ast, cursor, &else_block, errors)) {
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

static bool parse_while(const TokenList *tokens, Ast *ast, size_t *cursor,
                        size_t *out_index, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    size_t condition;
    size_t body;
    AstNode node = {0};

    (*cursor)++; /* past 'while' */
    if (!parse_expression(tokens, ast, cursor, &condition, errors)) {
        return false;
    }
    if (!parse_block(tokens, ast, cursor, &body, errors)) {
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

static bool parse_statement(const TokenList *tokens, Ast *ast, size_t *cursor,
                            size_t *out_index, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    AstNode node = {0};

    if (token == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }

    if (token->type == TOKEN_KEYWORD_RETURN) {
        size_t expression;

        (*cursor)++;
        if (!parse_expression(tokens, ast, cursor, &expression, errors)) {
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
        return parse_if(tokens, ast, cursor, out_index, errors);
    }
    if (token->type == TOKEN_KEYWORD_WHILE) {
        return parse_while(tokens, ast, cursor, out_index, errors);
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

    error_report_at(errors, token->pos,
                    MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
    return false;
}

/* VarDecl ::= 'var' IDENT (':' Type)? '=' Expression. The leading 'var'
   keyword has already been consumed; `*cursor` points at the variable name. */
static bool parse_var_declaration(const TokenList *tokens, Ast *ast,
                                  size_t *cursor, SourcePos declaration_pos,
                                  ErrorState *errors)
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
        if (!parse_type(tokens, ast, cursor, &type_index, errors)) {
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

    if (!parse_expression(tokens, ast, cursor, &initialiser_index, errors)) {
        return false;
    }

    declaration.kind = AST_DECLARATION;
    declaration.pos = declaration_pos;
    declaration.name_index = name_index;
    declaration.left_child = initialiser_index;
    declaration.right_child = type_index;
    return push_root(ast, &declaration, errors);
}

/* TypeDecl ::= 'type' IDENT Field+ 'end'
   Field ::= IDENT ':' IDENT. The leading 'type' keyword has already been
   consumed; `*cursor` points at the type name. */
static bool parse_type_declaration(const TokenList *tokens, Ast *ast,
                                   size_t *cursor, SourcePos declaration_pos,
                                   ErrorState *errors)
{
    const Token *token;
    size_t name_index;
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

    token = token_at(tokens, *cursor);
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
        AstNode type_node = {0};
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
        if (token == NULL || token->type != TOKEN_IDENTIFIER) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
            list_free(&fields);
            return false;
        }
        type_node.kind = AST_TYPE_NAME;
        type_node.pos = token->pos;
        if (!intern_token(ast, token, &field_type_index, errors)) {
            list_free(&fields);
            return false;
        }
        type_node.name_index = field_type_index;
        if (!push_node(ast, &type_node, &field_type_index)) {
            error_report(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&fields);
            return false;
        }
        (*cursor)++;

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
        /* Otherwise it is the next field name; continue. */
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
    return push_root(ast, &declaration, errors);
}

/* FunDecl ::= 'fun' IDENT '(' [ Param { ',' Param } ] ')' ':' Type
               'return' Expression 'end'
   Param ::= IDENT ':' Type. The leading 'fun' keyword has already been
   consumed; `*cursor` points at the function name. */
static bool parse_function_declaration(const TokenList *tokens, Ast *ast,
                                       size_t *cursor, SourcePos declaration_pos,
                                       ErrorState *errors)
{
    const Token *token;
    size_t name_index;
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

            if (!parse_type(tokens, ast, cursor, &parameter_type_index,
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

    if (!parse_type(tokens, ast, cursor, &return_type_index, errors)) {
        list_free(&parameters);
        return false;
    }

    if (!parse_block(tokens, ast, cursor, &body_index, errors)) {
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
    return push_root(ast, &declaration, errors);
}

/* Program ::= (VarDecl | TypeDecl | FunDecl)+ EOF */
bool parser_parse(const TokenList *tokens, Ast *ast, ErrorState *errors)
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

        if (token->type == TOKEN_KEYWORD_VAR) {
            cursor++;
            if (!parse_var_declaration(tokens, ast, &cursor, declaration_pos,
                                       errors)) {
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_TYPE) {
            cursor++;
            if (!parse_type_declaration(tokens, ast, &cursor, declaration_pos,
                                        errors)) {
                return false;
            }
        } else if (token->type == TOKEN_KEYWORD_FUN) {
            cursor++;
            if (!parse_function_declaration(tokens, ast, &cursor,
                                            declaration_pos, errors)) {
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
