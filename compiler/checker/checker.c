#include "checker_internal.h"


size_t *mapping_slot(List *mapping, size_t index)
{
    return (size_t *)list_get(mapping, index);
}


bool check_expression(CheckContext *ctx, size_t ast_index,
                             size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t *slot;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    slot = mapping_slot(ctx->mapping, ast_index);
    if (*slot != TYPED_NO_NODE) {
        *out_typed = *slot;
        return true;
    }

    {
        bool ok;

        switch (node->kind) {
        case AST_INTEGER_LITERAL:
        case AST_FLOAT_LITERAL:
            ok = check_literal(ctx, node, ast_index);
            break;
        case AST_BOOL_LITERAL:
            ok = check_bool_literal(ctx, node, ast_index);
            break;
        case AST_NIL_LITERAL:
            ok = check_nil_literal(ctx, node, ast_index);
            break;
        case AST_STRING_LITERAL:
            ok = check_string_literal(ctx, node, ast_index);
            break;
        case AST_LIST_LITERAL:
            ok = check_list_literal(ctx, node, ast_index);
            break;
        case AST_RECORD_LITERAL:
            ok = check_record_literal(ctx, node, ast_index);
            break;
        case AST_INDEX:
            ok = check_index_expression(ctx, node, ast_index);
            break;
        case AST_MEMBER_ACCESS:
            ok = check_member_access(ctx, node, ast_index);
            break;
        case AST_IS:
            ok = check_is(ctx, node, ast_index);
            break;
        case AST_IF_EXPRESSION:
            ok = check_if_expression(ctx, node, ast_index);
            break;
        case AST_MATCH_EXPRESSION:
            ok = check_match(ctx, node, ast_index, true, TYPED_NO_NODE,
                             &(size_t){0});
            break;
        case AST_MEMBER_CALL:
            ok = check_member_call(ctx, node, ast_index);
            break;
        case AST_UNARY:
            ok = check_unary(ctx, node, ast_index);
            break;
        case AST_IDENTIFIER:
            ok = check_identifier(ctx, node, ast_index);
            break;
        case AST_BINARY:
            ok = check_binary(ctx, node, ast_index);
            break;
        case AST_CALL:
            ok = check_call(ctx, node, ast_index);
            break;
        default:
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        if (ok) {
            *out_typed = *slot;
        }
        return ok;
    }
}


bool check_statement(CheckContext *ctx, size_t ast_index,
                            size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    TypedNode typed = {0};

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }

    if (node->kind == AST_RETURN) {
        size_t expression;
        const TypedNode *expression_node;

        size_t coerced;

        if (!check_expression(ctx, node->left_child, &expression)) {
            return false;
        }
        expression_node = typed_program_node(ctx->program, expression);
        if (expression_node == NULL ||
            !inject_into(ctx, expression, return_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RETURN_TYPE_MISMATCH,
                            ctx->function_name == NULL ? "function"
                                                       : ctx->function_name);
            return false;
        }
        expression = coerced;
        typed.kind = TYPED_RETURN;
        typed.type_index = return_type;
        typed.pos = node->pos;
        typed.left_child = expression;
        typed.right_child = TYPED_NO_NODE;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_IF) {
        size_t condition;
        size_t then_block;
        size_t else_block = TYPED_NO_NODE;
        size_t children_start;

        size_t narrowing_count;

        if (!check_condition(ctx, node->left_child, &condition)) {
            return false;
        }
        if (node->children_count == 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        narrowing_count = ctx->narrowings->length;
        if (!push_if_narrowing(ctx, node->left_child)) {
            return false;
        }
        {
            size_t then_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);

            if (!check_block(ctx, then_ast, return_type, &then_block)) {
                ctx->narrowings->length = narrowing_count;
                return false;
            }
        }
        ctx->narrowings->length = narrowing_count;
        if (node->children_count > 1) {
            size_t else_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + 1);

            if (!check_block(ctx, else_ast, return_type, &else_block)) {
                return false;
            }
        }

        children_start = ctx->program->children.length;
        if (!typed_program_push_child(ctx->program, then_block)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        if (else_block != TYPED_NO_NODE &&
            !typed_program_push_child(ctx->program, else_block)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }

        typed.kind = TYPED_IF;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = condition;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = node->children_count > 1 ? 2 : 1;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_MATCH) {
        return check_match(ctx, node, ast_index, false, return_type,
                           out_typed);
    }

    if (node->kind == AST_WHILE) {
        size_t condition;
        size_t body;

        if (!check_condition(ctx, node->left_child, &condition)) {
            return false;
        }
        ctx->loop_depth++;
        if (!check_block(ctx, node->right_child, return_type, &body)) {
            ctx->loop_depth--;
            return false;
        }
        ctx->loop_depth--;

        typed.kind = TYPED_WHILE;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = condition;
        typed.right_child = body;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    if (node->kind == AST_DECLARATION) {
        return check_local_declaration(ctx, node, ast_index, out_typed);
    }
    if (node->kind == AST_FOR) {
        return check_for(ctx, node, ast_index, return_type, out_typed);
    }
    if (node->kind == AST_ASSIGNMENT) {
        return check_assignment(ctx, node, ast_index, out_typed);
    }
    if (node->kind == AST_INDEX_ASSIGNMENT) {
        return check_index_assignment(ctx, node, ast_index, out_typed);
    }
    if (node->kind == AST_MEMBER_ASSIGNMENT) {
        return check_member_assignment(ctx, node, ast_index, out_typed);
    }

    if (node->kind == AST_EXPRESSION_STATEMENT) {
        return check_expression_statement(ctx, ast_index, out_typed);
    }

    if (node->kind == AST_BREAK || node->kind == AST_CONTINUE) {
        if (ctx->loop_depth == 0) {
            error_report_at(ctx->errors, node->pos,
                            node->kind == AST_BREAK
                                ? MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP
                                : MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
            return false;
        }
        typed.kind = node->kind == AST_BREAK ? TYPED_BREAK : TYPED_CONTINUE;
        typed.type_index = TYPED_NO_NODE;
        typed.pos = node->pos;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }

    error_report_at(ctx->errors, node->pos,
                    MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}



bool emit_node(CheckContext *ctx, const TypedNode *node,
                      size_t ast_index, size_t *out_index)
{
    if (!typed_program_push(ctx->program, node, out_index)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = *out_index;
    return true;
}

bool push_injection(CheckContext *ctx, size_t value, size_t target,
                           size_t tag, size_t *out_value)
{
    const TypedNode *value_node = typed_program_node(ctx->program, value);
    TypedNode inject = {0};

    inject.kind = TYPED_SUM_INJECT;
    inject.type_index = target;
    inject.pos = value_node == NULL ? (SourcePos){0, 1, 1} : value_node->pos;
    inject.integer_value = (int)tag;
    inject.name_index = TYPED_NO_NODE;
    inject.left_child = value;
    inject.right_child = TYPED_NO_NODE;
    if (!typed_program_push(ctx->program, &inject, out_value)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    return true;
}


/* A string literal takes the type its context gives it. The result of
   base64() has its type fixed and must stay a Text. */
static bool is_contextual_string(const CheckContext *ctx,
                                 const TypedNode *node)
{
    (void)ctx;
    return node->kind == TYPED_STRING_LITERAL && !node->fixed_type;
}


/* A pointer into the typed program (a node or a type) is only valid until the
   next thing is added to it: the array can move. Everything below that calls
   something that adds nodes or types (inject_into itself, push_injection,
   builtin_type, numeric_constant_retype) therefore reads what it needs first
   and fetches the pointers again afterwards. `make test-poison` finds a
   mistake here at once. */
bool inject_into(CheckContext *ctx, size_t value, size_t target,
                        size_t *out_value)
{
    TypedNode *value_node =
        (TypedNode *)typed_program_node(ctx->program, value);
    const TypedType *target_type = typed_program_type(ctx->program, target);
    size_t tag;

    if (value_node == NULL || target_type == NULL) {
        return false;
    }
    if (value_node->kind == TYPED_RECORD_LITERAL) {
        const TypedType *record = target_type;
        size_t key_type;
        size_t value_type;
        size_t entries = value_node->children_count / 2;
        size_t literal_start = value_node->children_start;
        size_t children_start;
        List values;
        size_t i;

        if (record->kind != NIPPY_TYPE_RECORD || record->child_count != 2) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
            return false;
        }
        if (value_node->children_count % 2 != 0 ||
            entries > record->fixed_length) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_RECORD_CAPACITY);
            return false;
        }
        key_type = *(const size_t *)list_get(&ctx->program->type_children,
                                             record->first_child);
        value_type = *(const size_t *)list_get(
            &ctx->program->type_children, record->first_child + 1);
        children_start = ctx->program->children.length;
        list_init(&values, sizeof(size_t));
        for (i = 0; i < entries; i++) {
            size_t key = *(const size_t *)list_get(
                &ctx->program->children, literal_start + i * 2);
            size_t item = *(const size_t *)list_get(
                &ctx->program->children, literal_start + i * 2 + 1);
            size_t coerced_key;
            size_t coerced_value;
            const TypedNode *key_node;
            size_t j;

            if (!inject_into(ctx, key, key_type, &coerced_key) ||
                !inject_into(ctx, item, value_type, &coerced_value)) {
                list_free(&values);
                return false;
            }
            key_node = typed_program_node(ctx->program, coerced_key);
            if (key_node == NULL ||
                key_node->string_offset + key_node->string_length >
                    ctx->program->bytes.length) {
                list_free(&values);
                return false;
            }
            for (j = 0; j < values.length; j += 2) {
                size_t previous = *(const size_t *)list_get(&values, j);
                const TypedNode *previous_node = typed_program_node(
                    ctx->program, previous);

                if (previous_node != NULL &&
                    previous_node->string_length == key_node->string_length &&
                    memcmp((const unsigned char *)ctx->program->bytes.items +
                               previous_node->string_offset,
                           (const unsigned char *)ctx->program->bytes.items +
                               key_node->string_offset,
                           key_node->string_length) == 0) {
                    error_report_at(ctx->errors, key_node->pos,
                                    MESSAGE_CHECKER_RECORD_DUPLICATE_KEY);
                    list_free(&values);
                    return false;
                }
            }
            if (!list_push(&values, &coerced_key) ||
                !list_push(&values, &coerced_value)) {
                list_free(&values);
                return false;
            }
        }
        for (i = 0; i < values.length; i++) {
            size_t child = *(const size_t *)list_get(&values, i);

            if (!typed_program_push_child(ctx->program, child)) {
                list_free(&values);
                error_report(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }
        value_node = (TypedNode *)typed_program_node(ctx->program, value);
        value_node->type_index = target;
        value_node->children_start = children_start;
        value_node->children_count = values.length;
        list_free(&values);
        *out_value = value;
        return true;
    }
    if (value_node->kind == TYPED_LIST_LITERAL) {
        if (target_type->kind == NIPPY_TYPE_LIST) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_LIST_LITERAL_CONTEXT);
            return false;
        }
        if (target_type->kind != NIPPY_TYPE_FIXED_LIST) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_LIST_LITERAL_CONTEXT);
            return false;
        }
        {
            const TypedType *fixed = target_type;
            size_t element_type;
            size_t element_count = value_node->children_count;
            size_t literal_start = value_node->children_start;
            size_t children_start = ctx->program->children.length;
            size_t i;

            if (fixed->child_count != 1) {
                return false;
            }
            if (element_count > fixed->fixed_length) {
                error_report_at(ctx->errors, value_node->pos,
                                MESSAGE_CHECKER_LIST_LITERAL_LENGTH,
                                element_count, (int)fixed->fixed_length);
                return false;
            }
            element_type = *(const size_t *)list_get(
                &ctx->program->type_children, fixed->first_child);
            for (i = 0; i < element_count; i++) {
                size_t child = *(const size_t *)list_get(
                    &ctx->program->children, literal_start + i);
                size_t coerced;

                if (!inject_into(ctx, child, element_type, &coerced) ||
                    !typed_program_push_child(ctx->program, coerced)) {
                    if (!ctx->errors->reported) {
                        error_report(ctx->errors,
                                     MESSAGE_CHECKER_LIST_VALUE);
                    }
                    return false;
                }
            }
            value_node = (TypedNode *)typed_program_node(ctx->program, value);
            value_node->type_index = target;
            value_node->children_start = children_start;
            *out_value = value;
            return true;
        }
    }
    if (same_type(ctx->program, value_node->type_index, target)) {
        *out_value = value;
        return true;
    }

    /* A literal, or a negated literal, takes the numeric type it is assigned
       to. This is literal typing, not a conversion: the result of an explicit
       conversion keeps the type it was given. */
    if (nippy_type_is_numeric(target_type->kind)) {
        NumericConstant constant;

        if (!numeric_constant_read(ctx->program, value, false, &constant) ||
            constant.is_float != nippy_type_is_float(target_type->kind)) {
            return false;
        }
        if (!numeric_constant_check_range(ctx, value_node->pos, &constant,
                                          target_type->kind) ||
            !numeric_constant_retype(ctx, value, target, target_type->kind)) {
            return false;
        }
        *out_value = value;
        return true;
    }

    if (target_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        if (!is_contextual_string(ctx, value_node)) {
            return false;
        }
        value_node->type_index = target;
        *out_value = value;
        return true;
    }

    if (target_type->kind == NIPPY_TYPE_STRING) {
        if (!is_contextual_string(ctx, value_node)) {
            return false;
        }
        if (value_node->string_length > target_type->fixed_length) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_STRING_TOO_LONG,
                            value_node->string_length,
                            (int)target_type->fixed_length);
            return false;
        }
        value_node->type_index = target;
        *out_value = value;
        return true;
    }

    if (target_type->kind != NIPPY_TYPE_UNION) {
        return false;
    }
    if (is_contextual_string(ctx, value_node)) {
        size_t fitting = TYPED_NO_NODE;
        size_t fitting_count = 0;
        size_t first_string = TYPED_NO_NODE;
        size_t first_capacity = 0;
        size_t i;

        for (i = 0; i < target_type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &ctx->program->type_children,
                target_type->first_child + i);
            const TypedType *alternative_type = typed_program_type(
                ctx->program, alternative);

            if (alternative_type == NULL) {
                continue;
            }
            if (alternative_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
                fitting = alternative;
                fitting_count++;
                continue;
            }
            if (alternative_type->kind != NIPPY_TYPE_STRING) {
                continue;
            }
            if (first_string == TYPED_NO_NODE) {
                first_string = alternative;
                first_capacity = alternative_type->fixed_length;
            }
            if (value_node->string_length <= alternative_type->fixed_length) {
                fitting = alternative;
                fitting_count++;
            }
        }
        if (fitting_count > 1) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_STRING_AMBIGUOUS);
            return false;
        }
        if (fitting_count == 1) {
            value_node->type_index = fitting;
            if (!union_has_alternative(ctx->program, target_type, fitting,
                                       &tag)) {
                return false;
            }
            return push_injection(ctx, value, target, tag, out_value);
        }
        if (first_string != TYPED_NO_NODE) {
            error_report_at(ctx->errors, value_node->pos,
                            MESSAGE_CHECKER_STRING_TOO_LONG,
                            value_node->string_length, (int)first_capacity);
            return false;
        }
    }
    if (union_has_alternative(ctx->program, target_type,
                              value_node->type_index, &tag)) {
        return push_injection(ctx, value, target, tag, out_value);
    }
    {
        /* A numeric literal in a union goes to the one alternative that can
           hold it. Two fitting alternatives are ambiguous, as for strings; an
           alternative of the right kind that is too small reports its range. */
        NumericConstant constant;

        if (numeric_constant_read(ctx->program, value, false, &constant)) {
            size_t fitting = TYPED_NO_NODE;
            size_t fitting_count = 0;
            NippyType fitting_kind = NIPPY_TYPE_UNKNOWN;
            NippyType first_kind = NIPPY_TYPE_UNKNOWN;
            size_t i;

            for (i = 0; i < target_type->child_count; i++) {
                size_t alternative = *(const size_t *)list_get(
                    &ctx->program->type_children,
                    target_type->first_child + i);
                const TypedType *alternative_type = typed_program_type(
                    ctx->program, alternative);

                if (alternative_type == NULL ||
                    !nippy_type_is_numeric(alternative_type->kind) ||
                    constant.is_float !=
                        nippy_type_is_float(alternative_type->kind)) {
                    continue;
                }
                if (first_kind == NIPPY_TYPE_UNKNOWN) {
                    first_kind = alternative_type->kind;
                }
                if (constant.is_float
                        ? float_constant_fits(alternative_type->kind,
                                              constant.real)
                        : integer_constant_fits(alternative_type->kind,
                                                constant.negative,
                                                constant.magnitude)) {
                    fitting = alternative;
                    fitting_kind = alternative_type->kind;
                    fitting_count++;
                }
            }
            if (fitting_count > 1) {
                error_report_at(ctx->errors, value_node->pos,
                                MESSAGE_CHECKER_NUMERIC_AMBIGUOUS);
                return false;
            }
            if (fitting_count == 1) {
                if (!numeric_constant_retype(ctx, value, fitting,
                                             fitting_kind)) {
                    return false;
                }
                /* Retyping can add a type, which may move the type array. */
                target_type = typed_program_type(ctx->program, target);
                if (target_type == NULL ||
                    !union_has_alternative(ctx->program, target_type, fitting,
                                           &tag)) {
                    return false;
                }
                return push_injection(ctx, value, target, tag, out_value);
            }
            if (first_kind != NIPPY_TYPE_UNKNOWN) {
                (void)numeric_constant_check_range(ctx, value_node->pos,
                                                   &constant, first_kind);
                return false;
            }
        }
    }
    return false;
}


bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors)
{
    CheckContext ctx = {0};
    TypeEnv empty_env = {0};
    List mapping;
    List types;
    List functions;
    List globals;
    List locals;
    List struct_instantiations;
    List function_instantiations;
    List narrowings;
    List stores;
    size_t i;
    bool ok = false;

    if (ast->roots.length == 0) {
        error_report(errors, MESSAGE_CHECKER_MISSING_AST_ROOT);
        return false;
    }

    list_init(&mapping, sizeof(size_t));
    list_init(&types, sizeof(TypeEntry));
    list_init(&functions, sizeof(FunctionEntry));
    list_init(&globals, sizeof(VariableEntry));
    list_init(&locals, sizeof(VariableEntry));
    list_init(&struct_instantiations, sizeof(StructInstantiation));
    list_init(&function_instantiations, sizeof(FunctionInstantiation));
    list_init(&narrowings, sizeof(Narrowing));
    list_init(&stores, sizeof(StoreEntry));

    ctx.ast = ast;
    ctx.program = program;
    ctx.errors = errors;
    ctx.mapping = &mapping;
    ctx.types = &types;
    ctx.functions = &functions;
    ctx.globals = &globals;
    ctx.locals = &locals;
    ctx.struct_instantiations = &struct_instantiations;
    ctx.function_instantiations = &function_instantiations;
    ctx.narrowings = &narrowings;
    ctx.stores = &stores;
    ctx.in_function = false;
    ctx.in_expression_statement = false;
    ctx.loop_depth = 0;
    ctx.function_name = NULL;
    ctx.depth = 0;
    ctx.generated_names = 0;
    ctx.env = &empty_env;

    for (i = 0; i < ast->nodes.length; i++) {
        size_t none = TYPED_NO_NODE;

        if (!list_push(&mapping, &none)) {
            error_report(errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    for (i = 0; i < ast->roots.length; i++) {
        size_t ast_root = *(const size_t *)list_get(&ast->roots, i);
        const AstNode *node = ast_get_node(ast, ast_root);
        size_t typed_root;
        bool checked = false;

        if (node == NULL) {
            error_report(errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_root);
            goto done;
        }

        switch (node->kind) {
        case AST_DECLARATION:
            checked = check_var_declaration(&ctx, node, ast_root);
            break;
        case AST_TYPE_DECLARATION:
        case AST_TYPE_ALIAS:
            checked = check_type_declaration(&ctx, node, ast_root);
            break;
        case AST_FUNCTION_DECLARATION:
            checked = check_function_declaration(&ctx, node, ast_root);
            break;
        case AST_EXPRESSION_STATEMENT:
            checked = check_expression_statement(&ctx, ast_root,
                                                 &(size_t){0});
            break;
        case AST_IF:
        case AST_MATCH:
        case AST_WHILE:
            checked = check_statement(&ctx, ast_root, TYPED_NO_NODE,
                                      &(size_t){0});
            break;
        case AST_FOR:
            checked = check_for(&ctx, node, ast_root, TYPED_NO_NODE,
                                &(size_t){0});
            break;
        case AST_ASSIGNMENT:
            checked = check_assignment(&ctx, node, ast_root, &(size_t){0});
            break;
        case AST_INDEX_ASSIGNMENT:
            checked = check_index_assignment(&ctx, node, ast_root,
                                             &(size_t){0});
            break;
        case AST_MEMBER_ASSIGNMENT:
            checked = check_member_assignment(&ctx, node, ast_root,
                                              &(size_t){0});
            break;
        default:
            error_report_at(errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            goto done;
        }
        if (!checked) {
            goto done;
        }
        if (node->kind == AST_TYPE_DECLARATION ||
            node->kind == AST_TYPE_ALIAS) {
            continue;
        }

        typed_root = *mapping_slot(&mapping, ast_root);
        if (typed_root == TYPED_NO_NODE) {
            /* A generic function is a template; its instantiations add their
               own concrete roots when called. */
            continue;
        }
        if (!list_push(&program->roots, &typed_root)) {
            error_report(errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }
    ok = true;

done:
    list_free(&mapping);
    list_free(&types);
    list_free(&functions);
    list_free(&globals);
    list_free(&locals);
    list_free(&struct_instantiations);
    list_free(&function_instantiations);
    list_free(&narrowings);
    list_free(&stores);
    return ok;
}
