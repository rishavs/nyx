#include "checker_internal.h"


bool check_assignment(CheckContext *ctx, const AstNode *node,
                             size_t ast_index, size_t *out_typed)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    const VariableEntry *variable;
    size_t value;
    size_t coerced;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL) {
        if (ctx->in_function && find_variable(ctx->globals, name) != NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, name);
        } else if (find_store(ctx->stores, name) != NULL ||
                   find_function(ctx->functions, name) != NULL ||
                   find_type(ctx->types, name) != NULL ||
                   is_builtin_type_name(name)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ASSIGN_TARGET, name);
        } else {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNKNOWN_NAME, name);
        }
        return false;
    }
    if (variable->is_const) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONST_ASSIGN, name);
        return false;
    }
    {
        size_t target_type = variable->type_index;

        if (!check_expression(ctx, node->left_child, &value)) {
            return false;
        }
        if (!inject_into(ctx, value, target_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ASSIGN_TYPE, name);
            return false;
        }
        typed.type_index = target_type;
    }
    typed.kind = TYPED_ASSIGNMENT;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = coerced;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    /* The right-hand side has been checked under the old narrowing. Any later
       use of the target must see its declared union type again. */
    invalidate_narrowing(ctx, name);
    return emit_node(ctx, &typed, ast_index, out_typed);
}


bool check_expression_statement(CheckContext *ctx, size_t ast_index,
                                       size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t expression;
    const TypedNode *expression_node;
    TypedNode typed = {0};

    if (node == NULL || node->kind != AST_EXPRESSION_STATEMENT) {
        error_report_at(ctx->errors,
                        node == NULL ? (SourcePos){0, 1, 1} : node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    {
        const AstNode *value = ast_get_node(ctx->ast, node->left_child);

        if (value == NULL ||
            (value->kind != AST_CALL && value->kind != AST_MEMBER_CALL)) {
            error_report_at(ctx->errors,
                            value == NULL ? node->pos : value->pos,
                            MESSAGE_CHECKER_EXPRESSION_STATEMENT);
            return false;
        }
    }
    {
        bool previous = ctx->in_expression_statement;

        ctx->in_expression_statement = true;
        if (!check_expression(ctx, node->left_child, &expression)) {
            ctx->in_expression_statement = previous;
            return false;
        }
        ctx->in_expression_statement = previous;
    }
    expression_node = typed_program_node(ctx->program, expression);
    if (expression_node != NULL &&
        type_contains_error(ctx->program, expression_node->type_index)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_DISCARDED_ERROR);
        return false;
    }
    typed.kind = TYPED_EXPRESSION_STATEMENT;
    typed.type_index = expression_node == NULL
                           ? TYPED_NO_NODE
                           : expression_node->type_index;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = expression;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, out_typed);
}


bool check_for_binding(CheckContext *ctx, const char *name,
                              size_t type_index, SourcePos pos)
{
    VariableEntry entry;

    if (name == NULL ||
        find_variable(ctx->locals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_type(ctx->types, name) != NULL ||
        find_store(ctx->stores, name) != NULL ||
        is_builtin_type_name(name) || is_builtin_function_name(name)) {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name == NULL ? "" : name);
        return false;
    }
    if (!check_reserved_name(ctx, pos, name)) {
        return false;
    }
    if (!check_type_value_size(ctx, type_index, pos)) {
        return false;
    }
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, name, strlen(name) + 1);
    entry.type_index = type_index;
    if (!list_push(ctx->locals, &entry)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}


static bool mutates_iterated_list(const Ast *ast, size_t ast_index,
                                  size_t symbol_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    if (node->kind == AST_MEMBER_CALL) {
        const AstNode *receiver = ast_get_node(ast, node->left_child);
        const char *method = ast_symbol_text(ast, node->name_index);

        if (receiver != NULL && receiver->kind == AST_IDENTIFIER &&
            receiver->name_index == symbol_index && method != NULL &&
            (strcmp(method, "push") == 0 || strcmp(method, "pop") == 0)) {
            return true;
        }
    }
    if (node->left_child != AST_NO_NODE &&
        mutates_iterated_list(ast, node->left_child, symbol_index)) {
        return true;
    }
    if (node->right_child != AST_NO_NODE &&
        mutates_iterated_list(ast, node->right_child, symbol_index)) {
        return true;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &ast->children, node->children_start + i);

        if (mutates_iterated_list(ast, child, symbol_index)) {
            return true;
        }
    }
    return false;
}


bool check_for(CheckContext *ctx, const AstNode *node,
                      size_t ast_index, size_t return_type,
                      size_t *out_typed)
{
    const char *first = ast_symbol_text(ctx->ast, node->name_index);
    const char *second = node->second_name_index == AST_NO_NODE
                             ? NULL
                             : ast_symbol_text(ctx->ast, node->second_name_index);
    size_t iterable;
    const TypedNode *iterable_node;
    const TypedType *iterable_type;
    size_t first_type;
    size_t second_type = TYPED_NO_NODE;
    size_t index_type;
    size_t scope_start;
    size_t body;
    size_t first_symbol;
    size_t second_symbol = TYPED_NO_NODE;
    size_t nil_type;
    bool is_map = false;
    bool is_list = false;
    TypedNode typed = {0};

    if (first == NULL ||
        !check_expression(ctx, node->left_child, &iterable)) {
        return false;
    }
    iterable_node = typed_program_node(ctx->program, iterable);
    iterable_type = iterable_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             iterable_node->type_index);
    if (iterable_type == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FOR_ITERABLE);
        return false;
    }
    if (iterable_type->kind == NIPPY_TYPE_RECORD ||
        iterable_type->kind == NIPPY_TYPE_TABLE) {
        if (iterable_type->child_count != 2) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FOR_ITERABLE);
            return false;
        }
        is_map = true;
        first_type = *(const size_t *)list_get(&ctx->program->type_children,
                                               iterable_type->first_child);
        second_type = *(const size_t *)list_get(
            &ctx->program->type_children, iterable_type->first_child + 1);
    } else if (iterable_type->kind == NIPPY_TYPE_LIST ||
               iterable_type->kind == NIPPY_TYPE_FIXED_LIST ||
               iterable_type->kind == NIPPY_TYPE_SLICE) {
        is_list = iterable_type->kind == NIPPY_TYPE_LIST ||
                  iterable_type->kind == NIPPY_TYPE_FIXED_LIST;
        if (iterable_type->child_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FOR_ITERABLE);
            return false;
        }
        first_type = *(const size_t *)list_get(
            &ctx->program->type_children, iterable_type->first_child);
    } else if (iterable_type->kind == NIPPY_TYPE_STRING ||
               iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        if (!builtin_type(ctx, NIPPY_TYPE_BYTE, &first_type)) {
            return false;
        }
    } else {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FOR_ITERABLE);
        return false;
    }
    if (!builtin_type(ctx, NIPPY_TYPE_INT32, &index_type) ||
        !builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        return false;
    }
    {
        const AstNode *iterable_ast = ast_get_node(ctx->ast, node->left_child);

        if (is_list && iterable_ast != NULL &&
            iterable_ast->kind == AST_IDENTIFIER &&
            mutates_iterated_list(ctx->ast, node->right_child,
                                  iterable_ast->name_index)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_FOR_MUTATION);
            return false;
        }
    }
    scope_start = ctx->locals->length;
    if (is_map) {
        if (!check_for_binding(ctx, first, first_type, node->pos)) {
            ctx->locals->length = scope_start;
            return false;
        }
        if (second != NULL &&
            !check_for_binding(ctx, second, second_type, node->pos)) {
            ctx->locals->length = scope_start;
            return false;
        }
        first_symbol = typed_program_copy_symbol(ctx->program, first,
                                                  strlen(first));
        if (first_symbol == TYPED_NO_NODE ||
            (second != NULL &&
             (second_symbol = typed_program_copy_symbol(
                  ctx->program, second, strlen(second))) ==
                 TYPED_NO_NODE)) {
            ctx->locals->length = scope_start;
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            return false;
        }
    } else if (second != NULL) {
        if (!check_for_binding(ctx, first, index_type, node->pos) ||
            !check_for_binding(ctx, second, first_type, node->pos)) {
            ctx->locals->length = scope_start;
            return false;
        }
        second_symbol = typed_program_copy_symbol(
            ctx->program, second, strlen(second));
        first_symbol = typed_program_copy_symbol(ctx->program, first,
                                                  strlen(first));
        if (first_symbol == TYPED_NO_NODE || second_symbol == TYPED_NO_NODE) {
            ctx->locals->length = scope_start;
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            return false;
        }
    } else {
        if (!check_for_binding(ctx, first, first_type, node->pos)) {
            ctx->locals->length = scope_start;
            return false;
        }
        first_symbol = typed_program_copy_symbol(ctx->program, first,
                                                  strlen(first));
        if (first_symbol == TYPED_NO_NODE) {
            ctx->locals->length = scope_start;
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            return false;
        }
    }
    ctx->loop_depth++;
    if (!check_block(ctx, node->right_child, return_type, &body)) {
        ctx->loop_depth--;
        ctx->locals->length = scope_start;
        return false;
    }
    ctx->loop_depth--;
    ctx->locals->length = scope_start;

    typed.kind = TYPED_FOR;
    typed.type_index = nil_type;
    typed.pos = node->pos;
    if (!is_map && second != NULL) {
        typed.name_index = second_symbol;
        typed.second_name_index = first_symbol;
    } else {
        typed.name_index = first_symbol;
        typed.second_name_index = second_symbol;
    }
    typed.left_child = iterable;
    typed.right_child = body;
    typed.auxiliary_type = first_type;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, out_typed);
}


bool check_local_declaration(CheckContext *ctx, const AstNode *node,
                                    size_t ast_index, size_t *out_typed)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t declared_type = TYPED_NO_NODE;
    size_t initialiser;
    const TypedNode *initialiser_node;
    size_t variable_type;
    VariableEntry entry;
    TypedNode typed = {0};

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (find_variable(ctx->locals, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_type(ctx->types, name) != NULL ||
        find_store(ctx->stores, name) != NULL ||
        is_builtin_type_name(name) || is_builtin_function_name(name)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_DUPLICATE_NAME, name);
        return false;
    }
    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_NO_INITIALISER, name);
        return false;
    }
    if (node->right_child != AST_NO_NODE &&
        !resolve_type_node(ctx, node->right_child, name, &declared_type)) {
        return false;
    }
    if (declared_type != TYPED_NO_NODE) {
        const TypedType *annotated = typed_program_type(ctx->program,
                                                        declared_type);

        if (annotated != NULL && annotated->kind == NIPPY_TYPE_STORE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LOCAL_STORE);
            return false;
        }
    }
    if (!check_expression(ctx, node->left_child, &initialiser)) {
        return false;
    }
    initialiser_node = typed_program_node(ctx->program, initialiser);
    if (initialiser_node == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_AST_NODE, node->left_child);
        return false;
    }
    if (declared_type == TYPED_NO_NODE) {
        const TypedType *inferred_type = typed_program_type(
            ctx->program, initialiser_node->type_index);

        if (initialiser_node->kind == TYPED_LIST_LITERAL &&
            initialiser_node->type_index == TYPED_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_INFER);
            return false;
        }
        if (initialiser_node->kind == TYPED_RECORD_LITERAL &&
            initialiser_node->type_index == TYPED_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
            return false;
        }
        if (inferred_type != NULL &&
            inferred_type->kind == NIPPY_TYPE_STRING_LITERAL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
            return false;
        }
        if (inferred_type != NULL &&
            inferred_type->kind == NIPPY_TYPE_STORE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LOCAL_STORE);
            return false;
        }
    }
    if (declared_type != TYPED_NO_NODE) {
        size_t coerced;

        if (!inject_into(ctx, initialiser, declared_type, &coerced)) {
            char declared_text[128];

            type_ast_text(ctx->ast, node->right_child, declared_text,
                          sizeof(declared_text));
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, name,
                            declared_text);
            return false;
        }
        initialiser = coerced;
        variable_type = declared_type;
    } else {
        variable_type = initialiser_node->type_index;
    }
    if (!check_type_value_size(ctx, variable_type, node->pos)) {
        return false;
    }
    typed.kind = TYPED_LOCAL_DECLARATION;
    typed.type_index = variable_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.left_child = initialiser;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    if (!emit_node(ctx, &typed, ast_index, out_typed)) {
        return false;
    }
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, name, strlen(name) + 1);
    entry.type_index = variable_type;
    entry.is_const = node->is_const;
    {
        const TypedType *type = typed_program_type(ctx->program,
                                                   variable_type);

        if (node->is_const && type != NULL &&
            type->kind == NIPPY_TYPE_INT32) {
            bool is_constant = false;
            int value;

            if (!const_int_eval(ctx, node->left_child, &value,
                                &is_constant)) {
                return false;
            }
            if (is_constant) {
                entry.has_constant = true;
                entry.constant_value = value;
            }
        }
    }
    if (!list_push(ctx->locals, &entry)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}


bool check_block(CheckContext *ctx, size_t ast_index,
                        size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    List statements;
    size_t scope_start = ctx->locals->length;
    size_t children_start;
    size_t i;
    TypedNode typed = {0};
    bool ok = false;

    if (node == NULL || node->kind != AST_BLOCK) {
        error_report_at(ctx->errors,
                        node == NULL ? (SourcePos){0, 1, 1} : node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }

    list_init(&statements, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t statement;

        if (!check_statement(ctx, child, return_type, &statement)) {
            goto done;
        }
        if (i + 1 < node->children_count &&
            typed_statement_terminates(ctx->program, statement)) {
            size_t next_child = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start + i + 1);
            const AstNode *next = ast_get_node(ctx->ast, next_child);

            error_report_at(ctx->errors,
                            next == NULL ? node->pos : next->pos,
                            MESSAGE_CHECKER_UNREACHABLE_CODE);
            goto done;
        }
        if (!list_push(&statements, &statement)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    children_start = ctx->program->children.length;
    for (i = 0; i < statements.length; i++) {
        size_t statement = *(const size_t *)list_get(&statements, i);

        if (!typed_program_push_child(ctx->program, statement)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    typed.kind = TYPED_BLOCK;
    typed.type_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = statements.length;
    ok = emit_node(ctx, &typed, ast_index, out_typed);

done:
    ctx->locals->length = scope_start;
    list_free(&statements);
    return ok;
}


static bool ast_contains_call_to(const Ast *ast, size_t ast_index,
                                size_t symbol_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    if (node->kind == AST_CALL && node->name_index == symbol_index) {
        return true;
    }
    if (node->left_child != AST_NO_NODE &&
        ast_contains_call_to(ast, node->left_child, symbol_index)) {
        return true;
    }
    if (node->right_child != AST_NO_NODE &&
        ast_contains_call_to(ast, node->right_child, symbol_index)) {
        return true;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &ast->children, node->children_start + i);

        if (ast_contains_call_to(ast, child, symbol_index)) {
            return true;
        }
    }
    return false;
}


static void ast_scan_returns(const Ast *ast, size_t ast_index,
                             size_t symbol_index, bool *has_return,
                             bool *has_safe_return)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    size_t i;

    if (node == NULL) {
        return;
    }
    if (node->kind == AST_RETURN) {
        *has_return = true;
        if (!ast_contains_call_to(ast, node->left_child, symbol_index)) {
            *has_safe_return = true;
        }
    }
    if (node->left_child != AST_NO_NODE) {
        ast_scan_returns(ast, node->left_child, symbol_index, has_return,
                         has_safe_return);
    }
    if (node->right_child != AST_NO_NODE) {
        ast_scan_returns(ast, node->right_child, symbol_index, has_return,
                         has_safe_return);
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &ast->children, node->children_start + i);

        ast_scan_returns(ast, child, symbol_index, has_return,
                         has_safe_return);
    }
}


bool check_function_declaration(CheckContext *ctx, const AstNode *node,
                                       size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    List parameter_types;
    size_t children_start = ctx->program->children.length;
    size_t return_type;
    size_t function_type;
    size_t typed_body;
    size_t typed_index;
    size_t i;
    TypedNode typed = {0};
    bool ok = false;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (find_type(ctx->types, name) != NULL ||
        find_function(ctx->functions, name) != NULL ||
        find_variable(ctx->globals, name) != NULL ||
        find_store(ctx->stores, name) != NULL ||
        is_builtin_type_name(name)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    if (is_builtin_function_name(name)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_BUILTIN_RESERVED, name);
        return false;
    }
    if (node->right_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }

    if (node->type_parameters_count > 0) {
        FunctionEntry generic = {0};

        if (node->type_parameters_count > NIPPY_MAX_TYPE_PARAMETERS) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        memcpy(generic.name, name, strlen(name) + 1);
        generic.type_index = TYPED_NO_NODE;
        generic.generic = true;
        generic.ast_index = ast_index;
        for (i = 0; i < node->type_parameters_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->ast->type_parameters,
                node->type_parameters_start + i);
            const AstNode *parameter = ast_get_node(ctx->ast, child);
            const char *parameter_name;

            if (parameter == NULL ||
                parameter->kind != AST_TYPE_PARAMETER) {
                error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                             child);
                return false;
            }
            parameter_name = ast_symbol_text(ctx->ast,
                                             parameter->name_index);
            if (parameter_name == NULL) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
                return false;
            }
            if (!check_reserved_name(ctx, parameter->pos, parameter_name)) {
                return false;
            }
            if (type_parameter_index(generic.parameters,
                                     generic.parameter_count,
                                     parameter_name) != TYPED_NO_NODE) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER,
                                parameter_name);
                return false;
            }
            memcpy(generic.parameters[generic.parameter_count],
                   parameter_name, strlen(parameter_name) + 1);
            generic.parameter_count++;
        }
        if (!list_push(ctx->functions, &generic)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
        return true;
    }

    list_init(&parameter_types, sizeof(size_t));
    ctx->in_function = true;
    ctx->locals->length = 0;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        const AstNode *parameter = ast_get_node(ctx->ast, child);
        const char *parameter_name;
        size_t parameter_type;
        size_t parameter_index;
        VariableEntry entry;
        TypedNode typed_parameter = {0};

        if (parameter == NULL || parameter->kind != AST_PARAMETER) {
            error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, child);
            goto done;
        }
        parameter_name = ast_symbol_text(ctx->ast, parameter->name_index);
        if (parameter_name == NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
            goto done;
        }
        if (!check_reserved_name(ctx, parameter->pos, parameter_name)) {
            goto done;
        }
        if (is_builtin_function_name(parameter_name)) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_BUILTIN_RESERVED, parameter_name);
            goto done;
        }
        if (find_variable(ctx->locals, parameter_name) != NULL ||
            find_store(ctx->stores, parameter_name) != NULL) {
            error_report_at(ctx->errors, parameter->pos,
                            MESSAGE_CHECKER_DUPLICATE_PARAMETER,
                            parameter_name);
            goto done;
        }
        if (!resolve_type_node(ctx, parameter->left_child, parameter_name,
                               &parameter_type)) {
            goto done;
        }
        {
            const TypedType *resolved =
                typed_program_type(ctx->program, parameter_type);

            if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
                error_report_at(ctx->errors, parameter->pos,
                                MESSAGE_CHECKER_STORE_TYPE);
                goto done;
            }
        }
        if (!check_type_value_size(ctx, parameter_type, parameter->pos)) {
            goto done;
        }

        typed_parameter.kind = TYPED_PARAMETER;
        typed_parameter.type_index = parameter_type;
        typed_parameter.pos = parameter->pos;
        typed_parameter.name_index = typed_program_copy_symbol(
            ctx->program, parameter_name, strlen(parameter_name));
        if (typed_parameter.name_index == TYPED_NO_NODE) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
            goto done;
        }
        typed_parameter.left_child = TYPED_NO_NODE;
        typed_parameter.right_child = TYPED_NO_NODE;
        typed_parameter.children_start = 0;
        typed_parameter.children_count = 0;
        if (!typed_program_push(ctx->program, &typed_parameter,
                                &parameter_index) ||
            !typed_program_push_child(ctx->program, parameter_index) ||
            !list_push(&parameter_types, &parameter_type)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            goto done;
        }

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, parameter_name, strlen(parameter_name) + 1);
        entry.type_index = parameter_type;
        if (!list_push(ctx->locals, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (!resolve_type_node(ctx, node->right_child, name, &return_type)) {
        goto done;
    }
    {
        const TypedType *resolved =
            typed_program_type(ctx->program, return_type);

        if (resolved != NULL && resolved->kind == NIPPY_TYPE_STORE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STORE_TYPE);
            goto done;
        }
    }
    if (!check_type_value_size(ctx, return_type, node->pos)) {
        goto done;
    }
    if (!add_function_type(ctx, &parameter_types, return_type, &function_type)) {
        goto done;
    }

    {
        FunctionEntry entry;

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name, strlen(name) + 1);
        entry.type_index = function_type;
        entry.generic = false;
        entry.ast_index = ast_index;
        if (!list_push(ctx->functions, &entry)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            goto done;
        }
    }

    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MISSING_RETURN,
                        name);
        goto done;
    }
    ctx->loop_depth = 0;
    ctx->function_name = name;
    if (!check_block(ctx, node->left_child, return_type, &typed_body)) {
        goto done;
    }
    if (!typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, name);
        goto done;
    }
    {
        bool has_return = false;
        bool has_safe_return = false;

        ast_scan_returns(ctx->ast, node->left_child, node->name_index,
                         &has_return, &has_safe_return);
        if (has_return && !has_safe_return) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALWAYS_RECURSIVE, name);
            goto done;
        }
    }

    typed.kind = TYPED_FUNCTION_DECLARATION;
    typed.type_index = function_type;
    typed.pos = node->pos;
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    if (typed.name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        goto done;
    }
    typed.left_child = typed_body;
    typed.right_child = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = node->children_count;

    if (!emit_node(ctx, &typed, ast_index, &typed_index)) {
        goto done;
    }
    ok = true;

done:
    ctx->in_function = false;
    ctx->loop_depth = 0;
    ctx->function_name = NULL;
    ctx->locals->length = 0;
    list_free(&parameter_types);
    return ok;
}
