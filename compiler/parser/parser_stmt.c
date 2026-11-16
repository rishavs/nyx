#include "parser_internal.h"


static bool parse_match_scan(size_t start, const TokenList *tokens, size_t *cursor, const Token **token, List *children, size_t *scan)
{
    (*scan) = start;
    size_t depth = 0;
    SourcePos failure = position_at(tokens, *cursor);
    while (((*token) = token_at(tokens, (*scan))) != NULL &&
           (*token)->type != TOKEN_EOF) {
        if ((*scan) >= *cursor &&
            (depth == 0 || (*token)->pos.line > failure.line) &&
            ((*token)->type == TOKEN_KEYWORD_WITH ||
             (*token)->type == TOKEN_KEYWORD_ELSE ||
             (*token)->type == TOKEN_KEYWORD_END)) break;
        if (token_opens_nesting((*token)->type)) depth++;
        else if (token_closes_nesting((*token)->type) && depth > 0) depth--;
        (*scan)++;
    }
    if ((*token) == NULL || (*token)->type == TOKEN_EOF) {
        list_free(children);
        return false;
    }
    return true;
}

static bool parse_match_token_at(const TokenList *tokens, size_t *cursor, bool expression_form, const List *string_bytes, Ast *ast, size_t *else_body, ErrorState *errors, List *children, size_t *children_start, AstNode *node, SourcePos *pos, bool type_match)
{
    if (token_at(tokens, *cursor) != NULL &&
        token_at(tokens, *cursor)->type == TOKEN_KEYWORD_ELSE) {
        (*cursor)++;
        if (expression_form) {
            if (!parse_expression(tokens, string_bytes, ast, cursor,
                                  else_body, errors)) {
                list_free(children);
                return false;
            }
        } else if (!parse_block(tokens, string_bytes, ast, cursor, else_body,
                                errors)) {
            list_free(children);
            return false;
        }
    } else if (expression_form) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_MATCH_EXPECTED_ELSE);
        list_free(children);
        return false;
    }
    if (token_at(tokens, *cursor) == NULL ||
        token_at(tokens, *cursor)->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        list_free(children);
        return false;
    }
    (*cursor)++;
    (*children_start) = ast->children.length;
    for (size_t i = 0; i < (*children).length; i++) {
        size_t child = *(const size_t *)list_get(children, i);

        if (!ast_push_child(ast, child)) {
            list_free(children);
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return false;
        }
    }
    list_free(children);
    (*node).kind = expression_form ? AST_MATCH_EXPRESSION : AST_MATCH;
    (*node).pos = (*pos);
    (*node).integer_value = type_match ? 1 : 0;
    return true;
}

static int parse_match_start(size_t *cursor, ErrorState *errors, bool type_match, const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *pattern, bool expression_form, size_t *body, List *children, size_t *error_start, size_t *start, const Token **token)
{
    (*start) = *cursor;
    (*error_start) = errors->length;
    

    if (type_match) {
        if (!parse_type(tokens, string_bytes, ast, cursor, pattern,
                        errors)) {
            return 1;
        }
    } else if (!parse_expression(tokens, string_bytes, ast, cursor,
                                 pattern, errors)) {
        return 1;
    }
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_THEN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_THEN);
        return 1;
    }
    (*cursor)++;
    if (expression_form) {
        if (!parse_expression(tokens, string_bytes, ast, cursor, body,
                              errors)) {
            return 1;
        }
    } else if (!parse_block(tokens, string_bytes, ast, cursor, body,
                            errors)) {
        return 1;
    }
    if (!list_push(children, pattern) ||
        !list_push(children, body)) {
        list_free(children);
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return 2;
    }
    return 0;
}

static bool parse_match_else_body(size_t *cursor, const TokenList *tokens, bool *type_match, const List *string_bytes, Ast *ast, size_t *head, ErrorState *errors, size_t *else_body, AstNode *node)
{
    (*else_body) = AST_NO_NODE;
    
    
    (*node) = (AstNode){0};

    (*cursor)++; /* past match */
    if (token_at(tokens, *cursor) != NULL &&
        token_at(tokens, *cursor)->type == TOKEN_KEYWORD_TYPEOF) {
        (*cursor)++;
        (*type_match) = true;
    }
    if (!parse_expression(tokens, string_bytes, ast, cursor, head, errors)) {
        return false;
    }
    if (token_at(tokens, *cursor) == NULL ||
        token_at(tokens, *cursor)->type != TOKEN_KEYWORD_WITH) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_WITH);
        return false;
    }
    return true;
}

bool parse_match(const TokenList *tokens, const List *string_bytes,
                        Ast *ast, size_t *cursor, bool expression_form,
                        size_t *out_index, ErrorState *errors)
{
    SourcePos pos = position_at(tokens, *cursor);
    bool type_match = false;
    size_t head;
    List children = {0};
    size_t else_body = {0};
    size_t children_start = {0};
    AstNode node = {0};
    if (!parse_match_else_body(cursor, tokens, &type_match, string_bytes, ast, &head, errors, &else_body, &node)) {
        return false;
    }
    (*cursor)++;
    list_init(&children, sizeof(size_t));
    for (;;) {
        size_t pattern;
        size_t body;
        size_t error_start = {0};
        size_t start = {0};
        const Token *token = {0};
        {
        int status = parse_match_start(cursor, errors, type_match, tokens, string_bytes, ast, &pattern, expression_form, &body, &children, &error_start, &start, &token);

        if (status == 1) {
            goto recover_arm;
        }
        if (status == 2) {
            return false;
        }
    }
        if (token_at(tokens, *cursor) != NULL &&
            token_at(tokens, *cursor)->type == TOKEN_KEYWORD_WITH) {
            (*cursor)++;
            continue;
        }
        break;

recover_arm:
        if (errors->length == error_start) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        }
        if (errors->fatal ||
            errors->length >= ERROR_MESSAGE_CAPACITY - 1) {
            list_free(&children);
            return false;
        }
        {
            size_t scan = {0};
            if (!parse_match_scan(start, tokens, cursor, &token, &children, &scan)) {
        return false;
    }
            *cursor = scan;
            if (token->type == TOKEN_KEYWORD_WITH) {
                (*cursor)++;
                continue;
            }
            break;
        }
    }
    if (!parse_match_token_at(tokens, cursor, expression_form, string_bytes, ast, &else_body, errors, &children, &children_start, &node, &pos, type_match)) {
        return false;
    }
    node.name_index = AST_NO_NODE;
    node.left_child = head;
    node.right_child = else_body;
    node.children_start = children_start;
    node.children_count = ast->children.length - children_start;
    return push_node(ast, &node, out_index);
}


static bool parse_if_expression_token(const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor, size_t *condition, ErrorState *errors, List *conditions, size_t *expression, List *expressions, const Token **token)
{
    if (!parse_expression(tokens, string_bytes, ast, cursor, condition,
                          errors) ||
        !list_push(conditions, condition)) {
        return true;
    }
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_THEN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_THEN);
        return true;
    }
    (*cursor)++;
    if (!parse_expression(tokens, string_bytes, ast, cursor, expression,
                          errors) ||
        !list_push(expressions, expression)) {
        return true;
    }
    return false;
}

static bool parse_if_expression_token4(const TokenList *tokens, size_t *cursor, ErrorState *errors, const List *string_bytes, Ast *ast, size_t *else_expression)
{
    {
        const Token *token = token_at(tokens, *cursor);

        if (token == NULL || token->type != TOKEN_KEYWORD_ELSE) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_ELSE);
            return true;
        }
        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor,
                              else_expression, errors)) {
            return true;
        }
        token = token_at(tokens, *cursor);
        if (token == NULL || token->type != TOKEN_KEYWORD_END) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
            return true;
        }
        (*cursor)++;
    }
    return false;
}

bool parse_if_expression(const TokenList *tokens,
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
        const Token *token = {0};
        if (parse_if_expression_token(tokens, string_bytes, ast, cursor, &condition, errors, &conditions, &expression, &expressions, &token)) {
        goto done;
    }
        token = token_at(tokens, *cursor);
        if (token != NULL && token->type == TOKEN_KEYWORD_ELSEIF) {
            (*cursor)++;
            continue;
        }
        break;
    }
    if (parse_if_expression_token4(tokens, cursor, errors, string_bytes, ast, &else_expression)) {
        goto done;
    }
    for (i = expressions.length; i > 0; i--) {
        AstNode node = {0};
        size_t condition = *(const size_t *)list_get(&conditions, i - 1);
        size_t expression = *(const size_t *)list_get(&expressions, i - 1);
        size_t children_start = ast->children.length;

        if (!ast_push_child(ast, else_expression)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
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

static void parse_block_token_opens_nesting(const Token *token, size_t *depth)
{
    if (token_opens_nesting(token->type)) (*depth)++;
    else if (token_closes_nesting(token->type) && (*depth) > 0) (*depth)--;
}

static bool parse_block_step(ErrorState *errors, size_t error_start, List *statements)
{
    if (errors->length == error_start) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
    }
    if (errors->fatal ||
        errors->length >= ERROR_MESSAGE_CAPACITY - 1) {
        list_free(statements);
        return false;
    }
    return true;
}

bool parse_block(
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
        size_t start = *cursor;
        size_t error_start = errors->length;

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
            size_t scan = start;
            size_t depth = 0;
            SourcePos failure = position_at(tokens, *cursor);

            if (!parse_block_step(errors, error_start, &statements)) {
        return false;
    }
            while ((token = token_at(tokens, scan)) != NULL &&
                   token->type != TOKEN_EOF) {
                if (scan >= *cursor && scan > start &&
                    (depth == 0 || token->pos.line > failure.line) &&
                    (token->type == TOKEN_KEYWORD_VAR ||
                     token->type == TOKEN_KEYWORD_CONST ||
                     token->type == TOKEN_KEYWORD_RETURN ||
                     token->type == TOKEN_KEYWORD_IF ||
                     token->type == TOKEN_KEYWORD_MATCH ||
                     token->type == TOKEN_KEYWORD_WHILE ||
                     token->type == TOKEN_KEYWORD_FOR ||
                     token->type == TOKEN_KEYWORD_BREAK ||
                     token->type == TOKEN_KEYWORD_CONTINUE ||
                     token->type == TOKEN_KEYWORD_END ||
                     token->type == TOKEN_KEYWORD_ELSE ||
                     token->type == TOKEN_KEYWORD_ELSEIF ||
                     token->type == TOKEN_KEYWORD_WITH ||
                     token_starts_recovery_statement(tokens, scan))) break;
                parse_block_token_opens_nesting(token, &depth);
                scan++;
            }
            *cursor = scan;
            if (token == NULL || token->type == TOKEN_EOF) {
                list_free(&statements);
                return false;
            }
            continue;
        }
        if (!list_push(&statements, &statement_index)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            list_free(&statements);
            return false;
        }
    }

    children_start = ast->children.length;
    for (i = 0; i < statements.length; i++) {
        size_t statement_index = *(const size_t *)list_get(&statements, i);

        if (!ast_push_child(ast, statement_index)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
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


bool wrap_in_block(Ast *ast, size_t statement_index, SourcePos pos,
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


bool make_if_node(Ast *ast, size_t condition, size_t then_block,
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


static int parse_if_token_at(const Token **token, const TokenList *tokens, size_t *cursor, ErrorState *errors, const List *string_bytes, Ast *ast, size_t *then_block, List *conditions, List *blocks, size_t *condition)
{
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_THEN) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_THEN);
        return 2;
    }
    (*cursor)++;

    if (!parse_block(tokens, string_bytes, ast, cursor, then_block, errors)) {
        return 2;
    }

    list_init(conditions, sizeof(size_t));
    list_init(blocks, sizeof(size_t));
    if (!list_push(conditions, condition) ||
        !list_push(blocks, then_block)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        return 1;
    }

    for (;;) {
        (*token) = token_at(tokens, *cursor);

        if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_ELSEIF) {
            break;
        }
        (*cursor)++;
        if (!parse_expression(tokens, string_bytes, ast, cursor, condition, errors)) {
            return 1;
        }
        (*token) = token_at(tokens, *cursor);
        if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_THEN) {
            error_report_at(errors, position_at(tokens, *cursor),
                            MESSAGE_PARSER_EXPECTED_THEN);
            return 1;
        }
        (*cursor)++;
        if (!parse_block(tokens, string_bytes, ast, cursor, then_block, errors)) {
            return 1;
        }
        if (!list_push(conditions, condition) ||
            !list_push(blocks, then_block)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            return 1;
        }
    }
    return 0;
}

bool parse_if(
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

    {
        int status = parse_if_token_at(&token, tokens, cursor, errors, string_bytes, ast, &then_block, &conditions, &blocks, &condition);

        if (status == 1) {
            goto done;
        }
        if (status == 2) {
            return false;
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
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            goto done;
        }
        if (!wrap_in_block(ast, nested_if, pos, &wrapper)) {
            error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
            goto done;
        }
        else_branch = wrapper;
    }

    if (!make_if_node(ast, *(const size_t *)list_get(&conditions, 0),
                      *(const size_t *)list_get(&blocks, 0), else_branch, pos,
                      out_index)) {
        error_report_fatal(errors, MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
        goto done;
    }
    ok = true;

done:
    list_free(&conditions);
    list_free(&blocks);
    return ok;
}


static bool parse_scope_intern_token(Ast *ast, const Token **token, size_t *name, ErrorState *errors, size_t *cursor, const TokenList *tokens)
{
    if (!intern_token(ast, (*token), name, errors)) return false;
    (*cursor)++;
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_EQUALS) {
        error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_EXPECTED_EQUALS);
        return false;
    }
    (*cursor)++;
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_IDENTIFIER || strcmp((*token)->text, "Store") != 0 ||
        token_at(tokens, *cursor + 1) == NULL ||
        token_at(tokens, *cursor + 1)->type != TOKEN_LPAREN) {
        error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_SCOPE_STORE);
        return false;
    }
    return true;
}

static bool parse_scope_node(size_t *cursor, const Token **token, const TokenList *tokens, ErrorState *errors, Ast *ast, size_t *name, const List *string_bytes, size_t *capacity, AstNode *node)
{
    (*node) = (AstNode){0};

    (*cursor)++;
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_IDENTIFIER) {
        error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
        return false;
    }
    if (!parse_scope_intern_token(ast, token, name, errors, cursor, tokens)) {
        return false;
    }
    *cursor += 2;
    if (!parse_expression(tokens, string_bytes, ast, cursor, capacity, errors)) return false;
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_RPAREN) {
        error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_EXPECTED_RPAREN);
        return false;
    }
    (*cursor)++;
    (*token) = token_at(tokens, *cursor);
    if ((*token) == NULL || (*token)->type != TOKEN_KEYWORD_DO) {
        error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_SCOPE_DO);
        return false;
    }
    return true;
}

bool parse_scope(const TokenList *tokens, const List *string_bytes, Ast *ast,
                 size_t *cursor, size_t *out_index, ErrorState *errors)
{
    const Token *token = token_at(tokens, *cursor);
    SourcePos pos = token->pos;
    size_t name, capacity, body;
    AstNode node = {0};
    if (!parse_scope_node(cursor, &token, tokens, errors, ast, &name, string_bytes, &capacity, &node)) {
        return false;
    }
    (*cursor)++;
    if (!parse_block(tokens, string_bytes, ast, cursor, &body, errors)) return false;
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_END) {
        error_report_at(errors, position_at(tokens, *cursor), MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        return false;
    }
    (*cursor)++;
    node.kind = AST_SCOPE;
    node.pos = pos;
    node.name_index = name;
    node.left_child = capacity;
    node.right_child = body;
    return push_node(ast, &node, out_index);
}


bool parse_while(
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
    token = token_at(tokens, *cursor);
    if (token == NULL || token->type != TOKEN_KEYWORD_DO) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_WHILE_DO);
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

    node.kind = AST_WHILE;
    node.pos = pos;
    node.left_child = condition;
    node.right_child = body;
    node.name_index = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    return push_node(ast, &node, out_index);
}


bool parse_for(
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


bool assignment_ahead(const TokenList *tokens, size_t cursor)
{
    const Token *name = token_at(tokens, cursor);
    const Token *equals = token_at(tokens, cursor + 1);

    return name != NULL && name->type == TOKEN_IDENTIFIER &&
           equals != NULL && equals->type == TOKEN_EQUALS;
}

bool index_assignment_ahead(const TokenList *tokens, size_t cursor)
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
bool parse_assignment(const TokenList *tokens,
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

bool parse_index_assignment(const TokenList *tokens,
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
bool field_assignment_ahead(const TokenList *tokens, size_t cursor)
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


bool parse_field_assignment(const TokenList *tokens,
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

bool parse_expression_statement(
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


static bool parse_statement_if_match(const Token *token, bool *outcome, const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor, size_t *out_index, ErrorState *errors, AstNode *node)
{
    if (token->type == TOKEN_KEYWORD_MATCH) {
        do { *outcome = (parse_match(tokens, string_bytes, ast, cursor, false,
                           out_index, errors)); return false; } while (0);
    }
    if (token->type == TOKEN_KEYWORD_SCOPE) {
        do { *outcome = (parse_scope(tokens, string_bytes, ast, cursor, out_index, errors)); return false; } while (0);
    }
    if (token->type == TOKEN_KEYWORD_WHILE) {
        do { *outcome = (parse_while(tokens, string_bytes, ast, cursor, out_index, errors)); return false; } while (0);
    }
    if (token->type == TOKEN_KEYWORD_FOR) {
        do { *outcome = (parse_for(tokens, string_bytes, ast, cursor, out_index, errors)); return false; } while (0);
    }
    if (token->type == TOKEN_KEYWORD_BREAK ||
        token->type == TOKEN_KEYWORD_CONTINUE) {
        (*cursor)++;
        (*node).kind = token->type == TOKEN_KEYWORD_BREAK ? AST_BREAK
                                                       : AST_CONTINUE;
        (*node).pos = token->pos;
        (*node).left_child = AST_NO_NODE;
        (*node).right_child = AST_NO_NODE;
        (*node).name_index = AST_NO_NODE;
        (*node).children_start = 0;
        (*node).children_count = 0;
        do { *outcome = (push_node(ast, node, out_index)); return false; } while (0);
    }
    return true;
}

static bool parse_statement_if(const Token *token, const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor, size_t *out_index, ErrorState *errors, AstNode *node, bool *outcome)
{
    if (token->type == TOKEN_KEYWORD_IF) {
        do { *outcome = (parse_if(tokens, string_bytes, ast, cursor, out_index, errors)); return true; } while (0);
    }
    if (!parse_statement_if_match(token, outcome, tokens, string_bytes, ast, cursor, out_index, errors, node)) {
        return true;
    }
    if (assignment_ahead(tokens, *cursor)) {
        do { *outcome = (parse_assignment(tokens, string_bytes, ast, cursor, out_index,
                                errors)); return true; } while (0);
    }
    if (index_assignment_ahead(tokens, *cursor)) {
        do { *outcome = (parse_index_assignment(tokens, string_bytes, ast, cursor,
                                      out_index, errors)); return true; } while (0);
    }
    if (field_assignment_ahead(tokens, *cursor)) {
        do { *outcome = (parse_field_assignment(tokens, string_bytes, ast, cursor,
                                      out_index, errors)); return true; } while (0);
    }
    if (argument_looks_like_expression(token)) {
        do { *outcome = (parse_expression_statement(tokens, string_bytes, ast, cursor,
                                          out_index, errors)); return true; } while (0);
    }
    return false;
}

static bool parse_statement_token(const TokenList *tokens, size_t *cursor, ErrorState *errors, const List *string_bytes, Ast *ast, size_t *out_index, const Token **token, AstNode *node, bool *outcome2)
{
    (*token) = token_at(tokens, *cursor);
    (*node) = (AstNode){0};

    if ((*token) == NULL) {
        error_report_at(errors, position_at(tokens, *cursor),
                        MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
        do { *outcome2 = (false); return true; } while (0);
    }

    if ((*token)->type == TOKEN_KEYWORD_VAR ||
        (*token)->type == TOKEN_KEYWORD_CONST) {
        bool is_const = (*token)->type == TOKEN_KEYWORD_CONST;

        (*cursor)++;
        do { *outcome2 = (parse_var_declaration(tokens, string_bytes, ast, cursor,
                                     (*token)->pos, false, is_const, true,
                                     out_index, errors)); return true; } while (0);
    }
    return false;
}

bool parse_statement(
    const TokenList *tokens, const List *string_bytes, Ast *ast, size_t *cursor,
    size_t *out_index, ErrorState *errors
)
{
    const Token *token = {0};
    AstNode node = {0};
    {
        bool outcome2;

        if (parse_statement_token(tokens, cursor, errors, string_bytes, ast, out_index, &token, &node, &outcome2)) {
            return outcome2;
        }
    }

    if (token->type == TOKEN_KEYWORD_RETURN) {
        size_t expression = AST_NO_NODE;
        SourcePos return_pos = token->pos;

        (*cursor)++;
        token = token_at(tokens, *cursor);
        if (token != NULL &&
            token->type != TOKEN_KEYWORD_END &&
            token->type != TOKEN_KEYWORD_ELSE &&
            token->type != TOKEN_KEYWORD_ELSEIF &&
            token->type != TOKEN_KEYWORD_WITH &&
            token->type != TOKEN_KEYWORD_BREAK &&
            token->type != TOKEN_KEYWORD_CONTINUE &&
            token->type != TOKEN_KEYWORD_VAR &&
            token->type != TOKEN_KEYWORD_CONST &&
            token->type != TOKEN_KEYWORD_WHILE &&
            token->type != TOKEN_KEYWORD_FOR &&
            token->type != TOKEN_KEYWORD_SCOPE &&
            token->type != TOKEN_KEYWORD_RETURN &&
            token->type != TOKEN_EOF &&
            !parse_expression(tokens, string_bytes, ast, cursor, &expression, errors)) {
            return false;
        }
        node.kind = AST_RETURN;
        node.pos = return_pos;
        node.left_child = expression;
        node.right_child = AST_NO_NODE;
        node.name_index = AST_NO_NODE;
        node.children_start = 0;
        node.children_count = 0;
        return push_node(ast, &node, out_index);
    }
    {
        bool outcome;

        if (parse_statement_if(token, tokens, string_bytes, ast, cursor, out_index, errors, &node, &outcome)) {
            return outcome;
        }
    }

    error_report_at(errors, token->pos,
                    MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
    return false;
}

bool parse_var_declaration(
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
        if (token == NULL ||
            (token->type != TOKEN_IDENTIFIER &&
             token->type != TOKEN_LPAREN)) {
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
