#include "checker_internal.h"

bool check_store_method(CheckContext *ctx, const AstNode *node,
    const char *method, size_t ast_index, bool *handled)
{
    const StoreEntry *store = NULL;
    TypedNode typed = {0};

    if (strcmp(method, "alloc") == 0) {
        *handled = true;
        size_t element;
        size_t value;
        size_t coerced;
        size_t handle;
        size_t argument_ast;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child == AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALLOC_TYPE_ARG);
            return false;
        }
        if (!resolve_type_in_env(ctx, node->right_child, ctx->env, "alloc",
                                 &element)) {
            return false;
        }
        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALLOC_ARG_COUNT);
            return false;
        }
        if (!handle_type(ctx, element, &handle)) {
            return false;
        }
        argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_expression(ctx, argument_ast, &value)) {
            return false;
        }
        if (!inject_into(ctx, value, element, &coerced)) {
            char element_text[128];

            type_ast_text(ctx->ast, node->right_child, element_text,
                          sizeof(element_text));
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ALLOC_ARG_TYPE, element_text);
            return false;
        }
        typed.kind = TYPED_STORE_ALLOC;
        typed.type_index = handle;
        typed.store_id = store->id;
        typed.integer_value = 0;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = coerced;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "reset") == 0) {
        *handled = true;
        size_t nil_type;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            return false;
        }
        typed.kind = TYPED_STORE_RESET;
        typed.type_index = nil_type;
        typed.store_id = store->id;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = TYPED_NO_NODE;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "read") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t element;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 0) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!check_expression(ctx, node->left_child, &receiver)) {
            return false;
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        receiver_type = receiver_node == NULL
                            ? NULL
                            : typed_program_type(ctx->program,
                                                 receiver_node->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_HANDLE ||
            receiver_type->child_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        element = *(const size_t *)list_get(&ctx->program->type_children,
                                            receiver_type->first_child);
        typed.kind = TYPED_HANDLE_READ;
        typed.type_index = element;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "write") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t element;
        size_t value;
        size_t coerced;
        size_t nil_type;
        size_t argument_ast;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!check_expression(ctx, node->left_child, &receiver)) {
            return false;
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        receiver_type = receiver_node == NULL
                            ? NULL
                            : typed_program_type(ctx->program,
                                                 receiver_node->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_HANDLE ||
            receiver_type->child_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        element = *(const size_t *)list_get(&ctx->program->type_children,
                                            receiver_type->first_child);
        argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_expression(ctx, argument_ast, &value)) {
            return false;
        }
        if (!inject_into(ctx, value, element, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_WRITE_TYPE);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            return false;
        }
        typed.kind = TYPED_HANDLE_WRITE;
        typed.type_index = nil_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = coerced;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

return true;
}
