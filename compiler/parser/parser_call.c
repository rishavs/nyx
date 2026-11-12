#include "parser_internal.h"

bool parse_call(
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


bool parse_record_literal(const TokenList *tokens,
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
