#include "checker_internal.h"



bool lookup_field(CheckContext *ctx, size_t receiver_type,
                          const char *name, size_t *out_type, bool *out_error)
{
    const TypedType *type = typed_program_type(ctx->program, receiver_type);

    if (type == NULL) {
        return false;
    }
    if (type->kind == NIPPY_TYPE_STRUCT) {
        const TypedNode *declaration = typed_program_node(
            ctx->program, type->declaration_node);
        size_t i;

        if (declaration == NULL) {
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &ctx->program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(ctx->program, child);
            const char *field_name;

            if (field == NULL || field->kind != TYPED_FIELD) {
                continue;
            }
            field_name = typed_program_symbol_text(ctx->program,
                                                   field->name_index);
            if (field_name != NULL && strcmp(field_name, name) == 0) {
                *out_type = field->type_index;
                *out_error = false;
                return true;
            }
        }
        return false;
    }
    if (type->kind == NIPPY_TYPE_ERROR) {
        static const char *const names[] = {
            "header", "description", "filename", "line", "col", "pos"
        };
        size_t i;

        for (i = 0; i < 6; i++) {
            if (strcmp(names[i], name) == 0 && i < type->child_count) {
                *out_type = *(const size_t *)list_get(
                    &ctx->program->type_children, type->first_child + i);
                *out_error = true;
                return true;
            }
        }
    }
    return false;
}


bool field_is_lvalue(const Ast *ast, size_t ast_index)
{
    const AstNode *node = ast_get_node(ast, ast_index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == AST_IDENTIFIER) {
        return true;
    }
    if (node->kind == AST_MEMBER_ACCESS) {
        return field_is_lvalue(ast, node->left_child);
    }
    return false;
}


bool check_member_access(CheckContext *ctx, const AstNode *node,
                                size_t ast_index)
{
    size_t receiver;
    const TypedNode *receiver_node;
    const TypedType *receiver_type;
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t field_type;
    bool is_error;
    TypedNode typed = {0};
    size_t name_index;

    if (name == NULL || !check_expression(ctx, node->left_child, &receiver)) {
        return false;
    }
    receiver_node = typed_program_node(ctx->program, receiver);
    receiver_type = receiver_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             receiver_node->type_index);
    if (receiver_type == NULL ||
        !lookup_field(ctx, receiver_node->type_index, name, &field_type,
                      &is_error)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNKNOWN_FIELD, name);
        return false;
    }
    name_index = typed_program_copy_symbol(ctx->program, name, strlen(name));
    if (name_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    typed.kind = TYPED_MEMBER_ACCESS;
    typed.type_index = field_type;
    typed.pos = node->pos;
    typed.name_index = name_index;
    typed.left_child = receiver;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_inline_const_mutation(CheckContext *ctx, size_t ast_index)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    const VariableEntry *variable;
    const TypedType *type;
    const char *name;

    while (node != NULL &&
           (node->kind == AST_MEMBER_ACCESS || node->kind == AST_INDEX)) {
        node = ast_get_node(ctx->ast, node->left_child);
    }
    if (node == NULL || node->kind != AST_IDENTIFIER) {
        return true;
    }
    name = ast_symbol_text(ctx->ast, node->name_index);
    if (name == NULL) {
        return true;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL || !variable->is_const) {
        return true;
    }
    type = typed_program_type(ctx->program, variable->type_index);
    if (type == NULL ||
        (type->kind != NIPPY_TYPE_STRUCT &&
         type->kind != NIPPY_TYPE_FIXED_LIST &&
         type->kind != NIPPY_TYPE_RECORD &&
         type->kind != NIPPY_TYPE_STRING)) {
        return true;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CONST_ASSIGN,
                    name);
    return false;
}


bool check_member_assignment(CheckContext *ctx, const AstNode *node,
                                    size_t ast_index, size_t *out_typed)
{
    const AstNode *target = ast_get_node(ctx->ast, node->left_child);
    size_t target_typed;
    const TypedNode *target_node;
    const TypedNode *receiver;
    const TypedType *receiver_type;
    size_t target_type_index;
    size_t value;
    size_t coerced;
    size_t nil_type;
    size_t previous_target = ctx->assignment_target_ast;
    TypedNode typed = {0};

    if (target == NULL || target->kind != AST_MEMBER_ACCESS ||
        !field_is_lvalue(ctx->ast, node->left_child)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    ctx->assignment_target_ast = node->left_child;
    if (!check_expression(ctx, node->left_child, &target_typed)) {
        ctx->assignment_target_ast = previous_target;
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    ctx->assignment_target_ast = previous_target;
    target_node = typed_program_node(ctx->program, target_typed);
    if (target_node == NULL || target_node->kind != TYPED_MEMBER_ACCESS) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    if (!check_inline_const_mutation(ctx, node->left_child)) {
        return false;
    }
    target_type_index = target_node->type_index;
    receiver = typed_program_node(ctx->program, target_node->left_child);
    receiver_type = receiver == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             receiver->type_index);
    if (receiver_type == NULL ||
        receiver_type->kind == NIPPY_TYPE_ERROR) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FIELD_LVALUE);
        return false;
    }
    if (!check_expression_expected(ctx, node->right_child, target_type_index,
                                   &value) ||
        !inject_into(ctx, value, target_type_index, &coerced) ||
        !builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        if (!ctx->errors->reported) {
            error_report(ctx->errors,
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH,
                         "field", "field");
        }
        return false;
    }
    typed.kind = TYPED_MEMBER_WRITE;
    typed.type_index = nil_type;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = target_typed;
    typed.right_child = coerced;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    invalidate_access_narrowing(ctx, node->left_child);
    return emit_node(ctx, &typed, ast_index, out_typed);
}


bool check_fixed_index_bounds(CheckContext *ctx, size_t index_ast,
                                     size_t receiver_type_index)
{
    const TypedType *type = typed_program_type(ctx->program,
                                               receiver_type_index);
    const AstNode *index_node = ast_get_node(ctx->ast, index_ast);
    int value;

    if (type == NULL || index_node == NULL ||
        (type->kind != NIPPY_TYPE_FIXED_LIST &&
         type->kind != NIPPY_TYPE_STRING) ||
        !literal_int_value(ctx->ast, index_ast, &value)) {
        return true;
    }
    if (value < 0 || (size_t)value >= type->fixed_length) {
        error_report_at(ctx->errors, index_node->pos,
                        MESSAGE_CHECKER_INDEX_BOUNDS, value,
                        type->fixed_length);
        return false;
    }
    return true;
}


bool check_index_expression(CheckContext *ctx, const AstNode *node,
                                   size_t ast_index)
{
    TypedNode typed = {0};
    size_t receiver;
    const TypedNode *receiver_node;
    const TypedType *receiver_type;
    size_t index;
    size_t element;
    size_t receiver_type_index;

    if (!check_expression(ctx, node->left_child, &receiver)) {
        return false;
    }
    receiver_node = typed_program_node(ctx->program, receiver);
    receiver_type_index = receiver_node == NULL
                              ? TYPED_NO_NODE
                              : receiver_node->type_index;
    receiver_type = receiver_type_index == TYPED_NO_NODE
                        ? NULL
                        : typed_program_type(ctx->program,
                                             receiver_type_index);
    if (receiver_type != NULL &&
        (receiver_type->kind == NIPPY_TYPE_RECORD ||
         receiver_type->kind == NIPPY_TYPE_TABLE)) {
        size_t key_type;
        size_t value_type;
        size_t key;
        size_t coerced;
        const TypedNode *key_node;
        const TypedType *key_node_type;

        if (receiver_type->child_count != 2) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, "index");
            return false;
        }
        key_type = *(const size_t *)list_get(&ctx->program->type_children,
                                             receiver_type->first_child);
        value_type = *(const size_t *)list_get(
            &ctx->program->type_children, receiver_type->first_child + 1);
        if (!check_expression(ctx, node->right_child, &key)) {
            return false;
        }
        key_node = typed_program_node(ctx->program, key);
        key_node_type = key_node == NULL
                            ? NULL
                            : typed_program_type(ctx->program,
                                                 key_node->type_index);
        if (key_node_type == NULL ||
            !inject_into(ctx, key, key_type, &coerced)) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MAP_KEY);
            }
            return false;
        }
        typed.kind = receiver_type->kind == NIPPY_TYPE_RECORD
                         ? TYPED_RECORD_GET
                         : TYPED_TABLE_GET;
        typed.type_index = value_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = coerced;
        typed.auxiliary_type = TYPED_NO_NODE;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }
    if (receiver_type == NULL ||
        (receiver_type->kind != NIPPY_TYPE_LIST &&
         receiver_type->kind != NIPPY_TYPE_FIXED_LIST &&
         receiver_type->kind != NIPPY_TYPE_SLICE) ||
        receiver_type->child_count != 1) {
        if (receiver_type != NULL &&
            (receiver_type->kind == NIPPY_TYPE_STRING ||
             receiver_type->kind == NIPPY_TYPE_DYNAMIC_STRING)) {
            error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_TEXT_INDEX);
            return false;
        }
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MEMBER_RECEIVER, "index");
        return false;
    }
    if (!check_fixed_index_bounds(ctx, node->right_child,
                                  receiver_type_index)) {
        return false;
    }
    if (!check_int32_expression(ctx, node->right_child, &index)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_LIST_INDEX);
        return false;
    }
    /* Checking the index can append types, growing the list and moving its
       storage, so a type pointer taken before this call is stale. */
    receiver_type = typed_program_type(ctx->program, receiver_type_index);
    if (receiver_type == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MEMBER_RECEIVER, "index");
        return false;
    }
    element = *(const size_t *)list_get(&ctx->program->type_children,
                                        receiver_type->first_child);
    typed.kind = receiver_type->kind == NIPPY_TYPE_SLICE
                     ? TYPED_SLICE_GET
                     : TYPED_LIST_GET;
    typed.type_index = element;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = receiver;
    typed.right_child = index;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = 0;
    typed.children_count = 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


bool check_index_assignment(CheckContext *ctx, const AstNode *node,
                                    size_t ast_index, size_t *out_typed)
{
    const AstNode *target = ast_get_node(ctx->ast, node->left_child);
    const AstNode *receiver_ast;
    size_t target_typed;
    const TypedNode *target_node;
    const TypedNode *receiver;
    const TypedType *receiver_type;
    size_t element;
    size_t value;
    size_t coerced;
    size_t nil_type;
    size_t children_start;
    size_t target_left_child;
    size_t target_right_child;
    size_t receiver_name_index;
    bool is_record;
    size_t previous_target = ctx->assignment_target_ast;
    TypedNode typed = {0};

    if (target == NULL || target->kind != AST_INDEX) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    receiver_ast = ast_get_node(ctx->ast, target->left_child);
    if (receiver_ast == NULL || receiver_ast->kind != AST_IDENTIFIER) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    if (!check_inline_const_mutation(ctx, node->left_child)) {
        return false;
    }
    ctx->assignment_target_ast = node->left_child;
    if (!check_expression(ctx, node->left_child, &target_typed)) {
        ctx->assignment_target_ast = previous_target;
        return false;
    }
    ctx->assignment_target_ast = previous_target;
    target_node = typed_program_node(ctx->program, target_typed);
    if (target_node == NULL ||
        (target_node->kind != TYPED_LIST_GET &&
         target_node->kind != TYPED_SLICE_GET &&
         target_node->kind != TYPED_RECORD_GET &&
         target_node->kind != TYPED_TABLE_GET)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    target_left_child = target_node->left_child;
    target_right_child = target_node->right_child;
    receiver = typed_program_node(ctx->program, target_left_child);
    receiver_type = receiver == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             receiver->type_index);
    receiver_name_index = receiver == NULL ? TYPED_NO_NODE
                                           : receiver->name_index;
    is_record = receiver_type != NULL &&
                receiver_type->kind == NIPPY_TYPE_RECORD;
    if (receiver_type != NULL &&
        (receiver_type->kind == NIPPY_TYPE_RECORD ||
         receiver_type->kind == NIPPY_TYPE_TABLE)) {
        size_t key_type;
        size_t value_type;
        size_t coerced_key;
        size_t coerced_value;

        if (receiver_type->child_count != 2 ||
            receiver_name_index == TYPED_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_LVALUE);
            return false;
        }
        key_type = *(const size_t *)list_get(&ctx->program->type_children,
                                             receiver_type->first_child);
        value_type = *(const size_t *)list_get(
            &ctx->program->type_children, receiver_type->first_child + 1);
        if (!inject_into(ctx, target_right_child, key_type, &coerced_key) ||
            !check_expression_expected(ctx, node->right_child, value_type,
                                       &value) ||
            !inject_into(ctx, value, value_type, &coerced_value) ||
            !builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MAP_VALUE);
            }
            return false;
        }
        children_start = ctx->program->children.length;
        if (!typed_program_push_child(ctx->program, coerced_key) ||
            !typed_program_push_child(ctx->program, coerced_value)) {
            error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            return false;
        }
        typed.kind = is_record ? TYPED_RECORD_SET : TYPED_TABLE_SET;
        typed.type_index = nil_type;
        typed.pos = node->pos;
        typed.name_index = receiver_name_index;
        typed.left_child = target_left_child;
        typed.right_child = TYPED_NO_NODE;
        typed.auxiliary_type = value_type;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = 2;
        return emit_node(ctx, &typed, ast_index, out_typed);
    }
    if (receiver_type == NULL || receiver_type->child_count != 1 ||
        receiver_name_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    if (receiver_type->kind == NIPPY_TYPE_SLICE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_SLICE_READONLY);
        return false;
    }
    /* The bounds check reads the index as a syntax node, so it needs the
       target's AST child; `target_right_child` is a typed-node index and numbers
       a different array. */
    if (!check_fixed_index_bounds(ctx, target->right_child,
                                  receiver->type_index)) {
        return false;
    }
    element = *(const size_t *)list_get(&ctx->program->type_children,
                                        receiver_type->first_child);
    if (!check_expression_expected(ctx, node->right_child, element, &value) ||
        !inject_into(ctx, value, element, &coerced) ||
        !builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
        if (!ctx->errors->reported) {
            error_report(ctx->errors, MESSAGE_CHECKER_LIST_VALUE);
        }
        return false;
    }
    children_start = ctx->program->children.length;
    if (!typed_program_push_child(ctx->program, target_right_child) ||
        !typed_program_push_child(ctx->program, coerced)) {
        error_report(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    typed.kind = TYPED_LIST_SET;
    typed.type_index = nil_type;
    typed.pos = node->pos;
    typed.name_index = receiver_name_index;
    typed.left_child = target_left_child;
    typed.right_child = TYPED_NO_NODE;
    typed.auxiliary_type = element;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = children_start;
    typed.children_count = 2;
    invalidate_access_narrowing(ctx, node->left_child);
    invalidate_aliased_narrowings(ctx);
    return emit_node(ctx, &typed, ast_index, out_typed);
}
