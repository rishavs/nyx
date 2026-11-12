#include "checker_internal.h"


bool typed_statement_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        return typed_block_returns(program, index);
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_returns(program, then_block) &&
               typed_statement_returns(program, else_block);
    }
    return false;
}


bool typed_statement_terminates(const TypedProgram *program,
                                       size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN || node->kind == TYPED_BREAK ||
        node->kind == TYPED_CONTINUE) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        size_t i;

        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);

            if (typed_statement_terminates(program, child)) {
                return true;
            }
        }
        return false;
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_terminates(program, then_block) &&
               typed_statement_terminates(program, else_block);
    }
    return false;
}


bool typed_block_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *block = typed_program_node(program, index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (typed_statement_returns(program, child)) {
            return true;
        }
        if (typed_statement_terminates(program, child)) {
            return false;
        }
    }
    return false;
}


bool check_condition(CheckContext *ctx, size_t ast_index,
                            size_t *out_typed)
{
    size_t expression;
    const TypedNode *node;
    const TypedType *type;

    if (!check_expression(ctx, ast_index, &expression)) {
        return false;
    }
    node = typed_program_node(ctx->program, expression);
    type = node == NULL ? NULL
                        : typed_program_type(ctx->program, node->type_index);
    if (type == NULL || type->kind != NIPPY_TYPE_BOOL) {
        const AstNode *ast_node = ast_get_node(ctx->ast, ast_index);

        error_report_at(ctx->errors,
                        ast_node == NULL ? (SourcePos){0, 1, 1} : ast_node->pos,
                        MESSAGE_CHECKER_CONDITION_TYPE);
        return false;
    }
    *out_typed = expression;
    return true;
}

bool push_if_narrowing(CheckContext *ctx, size_t condition_ast)
{
    const AstNode *condition = ast_get_node(ctx->ast, condition_ast);
    const AstNode *receiver;
    const char *name;
    const VariableEntry *variable;
    const TypedType *declared;
    size_t tested;
    size_t tag;

    if (condition == NULL || condition->kind != AST_IS) {
        return true;
    }
    receiver = ast_get_node(ctx->ast, condition->left_child);
    if (receiver == NULL || receiver->kind != AST_IDENTIFIER) {
        return true;
    }
    name = ast_symbol_text(ctx->ast, receiver->name_index);
    if (name == NULL) {
        return true;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL) {
        return true;
    }
    declared = typed_program_type(ctx->program, variable->type_index);
    if (declared == NULL || declared->kind != NIPPY_TYPE_UNION) {
        return true;
    }
    if (!resolve_type_in_env(ctx, condition->right_child, ctx->env, "is",
                             &tested)) {
        return false;
    }
    if (!union_has_alternative(ctx->program, declared, tested, &tag)) {
        return true;
    }
    {
        Narrowing narrowing = {0};

        narrowing.name = name;
        narrowing.type_index = tested;
        narrowing.tag = tag;
        narrowing.active = true;
        if (!list_push(ctx->narrowings, &narrowing)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}



void invalidate_narrowing(CheckContext *ctx, const char *name)
{
    size_t i;

    for (i = 0; i < ctx->narrowings->length; i++) {
        Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);

        if (narrowing != NULL && narrowing->active &&
            narrowing->name != NULL &&
            strcmp(narrowing->name, name) == 0) {
            /* Mark it inactive rather than removing it. The if-block
               restoration truncates by length; removing entries would let a
               nested block resurrect a stale narrowing. */
            narrowing->active = false;
        }
    }
}


bool check_if_expression(CheckContext *ctx, const AstNode *node,
                                size_t ast_index)
{
    size_t condition;
    size_t then_expression;
    size_t else_expression;
    const TypedNode *then_node;
    const TypedNode *else_node;
    size_t narrowing_start;
    TypedNode typed = {0};

    if (!check_condition(ctx, node->left_child, &condition)) {
        return false;
    }
    narrowing_start = ctx->narrowings->length;
    if (!push_if_narrowing(ctx, node->left_child)) {
        ctx->narrowings->length = narrowing_start;
        return false;
    }
    {
        bool ok = check_expression(ctx, node->right_child, &then_expression);

        ctx->narrowings->length = narrowing_start;
        if (!ok) {
            return false;
        }
    }
    if (!check_expression(ctx, *(const size_t *)list_get(
            &ctx->ast->children, node->children_start),
                         &else_expression)) {
        return false;
    }
    then_node = typed_program_node(ctx->program, then_expression);
    else_node = typed_program_node(ctx->program, else_expression);
    if (then_node == NULL || else_node == NULL ||
        !same_type(ctx->program, then_node->type_index,
                   else_node->type_index)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
        return false;
    }
    typed.kind = TYPED_IF_EXPRESSION;
    typed.type_index = then_node->type_index;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = condition;
    typed.right_child = then_expression;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 1;
    if (!typed_program_push_child(ctx->program, else_expression)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool push_match_narrowing(CheckContext *ctx, size_t head_ast,
                                  size_t tested, size_t tag)
{
    const AstNode *head = ast_get_node(ctx->ast, head_ast);
    const char *name;
    const VariableEntry *variable;
    const TypedType *declared;
    Narrowing narrowing = {0};

    if (head == NULL || head->kind != AST_IDENTIFIER) {
        return true;
    }
    name = ast_symbol_text(ctx->ast, head->name_index);
    if (name == NULL) {
        return true;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL) {
        return true;
    }
    declared = typed_program_type(ctx->program, variable->type_index);
    if (declared == NULL || declared->kind != NIPPY_TYPE_UNION) {
        return true;
    }
    narrowing.name = name;
    narrowing.type_index = tested;
    narrowing.tag = tag;
    narrowing.active = true;
    if (!list_push(ctx->narrowings, &narrowing)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}


bool check_match(CheckContext *ctx, const AstNode *node,
                        size_t ast_index, bool expression_form,
                        size_t return_type, size_t *out_typed)
{
    bool type_match = node->integer_value != 0;
    size_t head;
    const TypedNode *head_node;
    const TypedType *head_type;
    size_t bool_type;
    size_t nil_type;
    size_t else_typed = TYPED_NO_NODE;
    size_t else_type = TYPED_NO_NODE;
    bool has_else = node->right_child != AST_NO_NODE;
    bool exhaustive = has_else;
    List conditions;
    List bodies;
    List body_types;
    size_t i;
    size_t chain = TYPED_NO_NODE;

    if (!check_expression(ctx, node->left_child, &head)) {
        return false;
    }
    head_node = typed_program_node(ctx->program, head);
    head_type = head_node == NULL
                    ? NULL
                    : typed_program_type(ctx->program, head_node->type_index);
    if (head_type == NULL ||
        !builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type) ||
        !builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    list_init(&conditions, sizeof(size_t));
    list_init(&bodies, sizeof(size_t));
    list_init(&body_types, sizeof(size_t));
    for (i = 0; i < node->children_count; i += 2) {
        size_t pattern_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t body_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i + 1);
        size_t condition;
        head_node = typed_program_node(ctx->program, head);
        head_type = head_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             head_node->type_index);
        if (head_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
        size_t body;
        size_t body_type;
        size_t narrowing_start = ctx->narrowings->length;
        TypedNode test = {0};

        if (type_match) {
            size_t tested;
            size_t tag = 0;
            size_t j;

            if (!resolve_type_in_env(ctx, pattern_ast, ctx->env, "match",
                                     &tested)) {
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            if (head_type->kind == NIPPY_TYPE_UNION) {
                if (!union_has_alternative(ctx->program, head_type, tested,
                                           &tag)) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MATCH_PATTERN);
                    list_free(&conditions);
                    list_free(&bodies);
                    list_free(&body_types);
                    return false;
                }
            } else if (!same_type(ctx->program, head_node->type_index,
                                  tested)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MATCH_PATTERN);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            for (j = 0; j < i; j += 2) {
                size_t previous_ast = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + j);
                size_t previous;

                if (!resolve_type_in_env(ctx, previous_ast, ctx->env, "match",
                                         &previous)) {
                    list_free(&conditions);
                    list_free(&bodies);
                    list_free(&body_types);
                    return false;
                }
                if (same_type(ctx->program, previous, tested)) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MATCH_DUPLICATE);
                    list_free(&conditions);
                    list_free(&bodies);
                    list_free(&body_types);
                    return false;
                }
            }
            test.kind = TYPED_IS;
            test.type_index = bool_type;
            test.auxiliary_type = tested;
            test.left_child = head;
            test.integer_value = head_type->kind == NIPPY_TYPE_UNION
                                     ? (int)tag
                                     : 1;
            test.pos = node->pos;
            if (!typed_program_push(ctx->program, &test, &condition)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            if (!push_match_narrowing(ctx, node->left_child, tested, tag)) {
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
        } else {
            size_t pattern;
            size_t coerced;
            const TypedNode *pattern_node;

            if (!check_expression(ctx, pattern_ast, &pattern)) {
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            pattern_node = typed_program_node(ctx->program, pattern);
            if (pattern_node == NULL ||
                !inject_into(ctx, pattern, head_node->type_index, &coerced)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MATCH_PATTERN);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            test.kind = TYPED_BINARY;
            test.op = NIPPY_OPERATOR_EQ;
            test.type_index = bool_type;
            test.left_child = head;
            test.right_child = coerced;
            test.pos = node->pos;
            if (!typed_program_push(ctx->program, &test, &condition)) {
                error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
        }
        if (expression_form) {
            const TypedNode *body_node;

            if (!check_expression(ctx, body_ast, &body)) {
                ctx->narrowings->length = narrowing_start;
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            body_node = typed_program_node(ctx->program, body);
            body_type = body_node == NULL ? TYPED_NO_NODE
                                          : body_node->type_index;
            if (body_type == TYPED_NO_NODE ||
                (body_types.length > 0 &&
                 !same_type(ctx->program,
                            *(const size_t *)list_get(&body_types, 0),
                            body_type))) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_IF_EXPR_MISMATCH);
                ctx->narrowings->length = narrowing_start;
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
        } else if (!check_block(ctx, body_ast, return_type, &body)) {
            ctx->narrowings->length = narrowing_start;
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        } else {
            body_type = TYPED_NO_NODE;
        }
        ctx->narrowings->length = narrowing_start;
        if (!list_push(&conditions, &condition) ||
            !list_push(&bodies, &body) ||
            !list_push(&body_types, &body_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
    }
    if (!has_else) {
        head_node = typed_program_node(ctx->program, head);
        head_type = head_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             head_node->type_index);
        if (head_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
        if (type_match && head_type->kind == NIPPY_TYPE_UNION) {
            exhaustive = true;
            for (i = 0; i < head_type->child_count; i++) {
                size_t alternative = *(const size_t *)list_get(
                    &ctx->program->type_children,
                    head_type->first_child + i);
                size_t j;
                bool found = false;

                for (j = 0; j < node->children_count; j += 2) {
                    size_t pattern_ast = *(const size_t *)list_get(
                        &ctx->ast->children, node->children_start + j);
                    size_t pattern_type;

                    if (resolve_type_in_env(ctx, pattern_ast, ctx->env,
                                            "match", &pattern_type) &&
                        same_type(ctx->program, alternative, pattern_type)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    exhaustive = false;
                    break;
                }
            }
        } else if (!type_match &&
                   same_type(ctx->program, head_node->type_index,
                             bool_type)) {
            bool seen_true = false;
            bool seen_false = false;

            for (i = 0; i < node->children_count; i += 2) {
                size_t pattern_ast = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + i);
                const AstNode *pattern = ast_get_node(ctx->ast, pattern_ast);

                if (pattern != NULL && pattern->kind == AST_BOOL_LITERAL) {
                    if (pattern->integer_value != 0) {
                        seen_true = true;
                    } else {
                        seen_false = true;
                    }
                }
            }
            exhaustive = seen_true && seen_false;
        } else if (!type_match &&
                   same_type(ctx->program, head_node->type_index,
                             nil_type)) {
            exhaustive = true;
        } else if (type_match && head_type->kind != NIPPY_TYPE_UNION) {
            exhaustive = body_types.length > 0;
        }
    }
    if (has_else) {
        if (expression_form) {
            const TypedNode *else_node;

            if (!check_expression(ctx, node->right_child, &else_typed)) {
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            else_node = typed_program_node(ctx->program, else_typed);
            else_type = else_node == NULL ? TYPED_NO_NODE
                                          : else_node->type_index;
            if (else_type == TYPED_NO_NODE ||
                (body_types.length > 0 &&
                 !same_type(ctx->program,
                            *(const size_t *)list_get(&body_types, 0),
                            else_type))) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_IF_EXPR_MISMATCH);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
        } else if (!check_block(ctx, node->right_child, return_type,
                                &else_typed)) {
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
    }
    if (!exhaustive) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
        list_free(&conditions);
        list_free(&bodies);
        list_free(&body_types);
        return false;
    }
    chain = else_typed;
    for (i = bodies.length; i > 0; i--) {
        size_t condition = *(const size_t *)list_get(&conditions, i - 1);
        size_t body = *(const size_t *)list_get(&bodies, i - 1);
        TypedNode lowered = {0};

        lowered.pos = node->pos;
        lowered.left_child = condition;
        if (expression_form) {
            size_t children_start = ctx->program->children.length;

            if (chain == TYPED_NO_NODE ||
                !typed_program_push_child(ctx->program, chain)) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            lowered.kind = TYPED_IF_EXPRESSION;
            lowered.type_index = *(const size_t *)list_get(&body_types, i - 1);
            lowered.right_child = body;
            lowered.children_start = children_start;
            lowered.children_count = 1;
        } else {
            size_t children_start;

            if (chain != TYPED_NO_NODE) {
                const TypedNode *chain_node =
                    typed_program_node(ctx->program, chain);

                if (chain_node != NULL && chain_node->kind != TYPED_BLOCK) {
                    TypedNode wrapper = {0};
                    size_t wrapper_start = ctx->program->children.length;
                    size_t wrapped;

                    if (!typed_program_push_child(ctx->program, chain)) {
                        error_report(ctx->errors,
                                     MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                        list_free(&conditions);
                        list_free(&bodies);
                        list_free(&body_types);
                        return false;
                    }
                    wrapper.kind = TYPED_BLOCK;
                    wrapper.type_index = TYPED_NO_NODE;
                    wrapper.pos = node->pos;
                    wrapper.children_start = wrapper_start;
                    wrapper.children_count = 1;
                    if (!typed_program_push(ctx->program, &wrapper, &wrapped)) {
                        error_report(ctx->errors,
                                     MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                        list_free(&conditions);
                        list_free(&bodies);
                        list_free(&body_types);
                        return false;
                    }
                    chain = wrapped;
                }
            }
            children_start = ctx->program->children.length;
            if (!typed_program_push_child(ctx->program, body) ||
                (chain != TYPED_NO_NODE &&
                 !typed_program_push_child(ctx->program, chain))) {
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            lowered.kind = TYPED_IF;
            lowered.type_index = TYPED_NO_NODE;
            lowered.children_start = children_start;
            lowered.children_count =
                chain == TYPED_NO_NODE ? 1 : 2;
        }
        if (!typed_program_push(ctx->program, &lowered, &chain)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
    }
    list_free(&conditions);
    list_free(&bodies);
    list_free(&body_types);
    if (chain == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = chain;
    *out_typed = chain;
    return true;
}
