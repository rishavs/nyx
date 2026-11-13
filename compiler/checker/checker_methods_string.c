#include "checker_internal.h"

bool check_string_method(CheckContext *ctx, const AstNode *node,
    const char *method, size_t ast_index, bool *handled)
{
    const StoreEntry *store = NULL;
    TypedNode typed = {0};

    if (strcmp(method, "append") == 0 ||
        strcmp(method, "push") == 0 ||
        strcmp(method, "append_byte") == 0 ||
        strcmp(method, "finish") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t receiver_name_index;
        size_t argument_count = strcmp(method, "finish") == 0 ? 0u : 1u;
        size_t nil_type;
        bool is_append = strcmp(method, "append") == 0 ||
                         strcmp(method, "push") == 0;
        bool is_list_push = strcmp(method, "push") == 0;
        bool is_byte = strcmp(method, "append_byte") == 0;
        size_t argument = TYPED_NO_NODE;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != argument_count) {
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
        receiver_name_index = receiver_node == NULL
                                  ? TYPED_NO_NODE
                                  : receiver_node->name_index;
        if (is_list_push && receiver_type != NULL &&
            (receiver_type->kind == NIPPY_TYPE_LIST ||
             receiver_type->kind == NIPPY_TYPE_FIXED_LIST)) {
            const AstNode *receiver_ast = ast_get_node(
                ctx->ast, node->left_child);
            size_t element;
            size_t argument_ast;
            size_t value;
            size_t coerced;
            size_t list_nil;

            if (receiver_ast == NULL ||
                receiver_ast->kind != AST_IDENTIFIER ||
                receiver_node->name_index == TYPED_NO_NODE) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_LIST_LVALUE);
                return false;
            }
            if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST &&
                !check_inline_const_mutation(ctx, node->left_child)) {
                return false;
            }
            if (receiver_type->child_count != 1) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MEMBER_RECEIVER, method);
                return false;
            }
            element = *(const size_t *)list_get(
                &ctx->program->type_children, receiver_type->first_child);
            argument_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);
            if (!check_expression(ctx, argument_ast, &value) ||
                !inject_into(ctx, value, element, &coerced) ||
                !builtin_type(ctx, NIPPY_TYPE_NIL, &list_nil)) {
                if (!ctx->errors->reported) {
                    error_report(ctx->errors,
                                 MESSAGE_CHECKER_LIST_VALUE);
                }
                return false;
            }
            typed.kind = TYPED_LIST_PUSH;
            typed.type_index = list_nil;
            typed.pos = node->pos;
            typed.name_index = receiver_name_index;
            typed.left_child = receiver;
            typed.right_child = coerced;
            typed.auxiliary_type = element;
            typed.store_id = TYPED_NO_NODE;
            typed.children_start = 0;
            typed.children_count = 0;
            return emit_node(ctx, &typed, ast_index, &(size_t){0});
        }
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_TEXT_BUILDER ||
            is_list_push) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (argument_count == 1) {
            size_t argument_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);
            size_t checked;

            if (!check_expression(ctx, argument_ast, &checked)) {
                return false;
            }
            if (is_append) {
                size_t dynamic_type;

                if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING,
                                  &dynamic_type) ||
                    !inject_into(ctx, checked, dynamic_type, &argument)) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_BUILDER_APPEND_TYPE);
                    return false;
                }
            } else if (is_byte) {
                size_t byte_type;

                if (!builtin_type(ctx, NIPPY_TYPE_BYTE, &byte_type) ||
                    !inject_into(ctx, checked, byte_type, &argument)) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_BUILDER_BYTE_TYPE);
                    return false;
                }
            } else {
                argument = checked;
            }
        }
        if (strcmp(method, "finish") == 0) {
            size_t dynamic_type;

            if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING,
                              &dynamic_type)) {
                return false;
            }
            typed.type_index = dynamic_type;
            typed.kind = TYPED_BUILDER_FINISH;
        } else {
            if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
                return false;
            }
            typed.type_index = nil_type;
            typed.kind = is_append ? TYPED_BUILDER_APPEND
                                   : TYPED_BUILDER_APPEND_BYTE;
        }
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = argument;
        typed.auxiliary_type = TYPED_NO_NODE;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "builder") == 0) {
        *handled = true;
        size_t builder_type;
        size_t capacity_ast;
        size_t capacity;
        size_t int32_type;
        size_t coerced;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
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
        capacity_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_negative_capacity(ctx, capacity_ast)) {
            return false;
        }
        if (!check_expression(ctx, capacity_ast, &capacity)) {
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type) ||
            !inject_into(ctx, capacity, int32_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_BUILDER_CAPACITY);
            return false;
        }
        if (!text_builder_type(ctx, &builder_type)) {
            return false;
        }
        typed.kind = TYPED_BUILDER_CREATE;
        typed.type_index = builder_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = coerced;
        typed.right_child = TYPED_NO_NODE;
        typed.auxiliary_type = TYPED_NO_NODE;
        typed.store_id = store->id;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "concat") == 0 ||
        strcmp(method, "copy") == 0 ||
        strcmp(method, "text") == 0) {
        *handled = true;
        size_t dynamic_type;
        size_t argument_count = strcmp(method, "concat") == 0 ? 2u : 1u;
        size_t argument;
        size_t coerced;
        size_t first;
        size_t second = TYPED_NO_NODE;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != argument_count) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_ARGS, method);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, &dynamic_type)) {
            return false;
        }
        first = *(const size_t *)list_get(&ctx->ast->children,
                                          node->children_start);
        if (!check_expression(ctx, first, &argument)) {
            return false;
        }
        {
            const TypedNode *argument_node = typed_program_node(
                ctx->program, argument);
            const TypedType *argument_type =
                argument_node == NULL
                    ? NULL
                    : typed_program_type(ctx->program,
                                         argument_node->type_index);

            if (argument_type == NULL) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
                return false;
            }
            if (strcmp(method, "text") == 0) {
                if (argument_type->kind != NIPPY_TYPE_STRING) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_STORE_TEXT_TYPE);
                    return false;
                }
                coerced = argument;
            } else if (!inject_into(ctx, argument, dynamic_type, &coerced)) {
                error_report_at(ctx->errors, node->pos,
                                strcmp(method, "concat") == 0
                                    ? MESSAGE_CHECKER_CONCAT_TYPE
                                    : MESSAGE_CHECKER_COPY_TYPE);
                return false;
            }
        }
        if (argument_count == 2) {
            second = *(const size_t *)list_get(&ctx->ast->children,
                                               node->children_start + 1);
            if (!check_expression(ctx, second, &argument)) {
                return false;
            }
            if (!inject_into(ctx, argument, dynamic_type, &second)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_CONCAT_TYPE);
                return false;
            }
        }
        typed.kind = strcmp(method, "concat") == 0
                         ? TYPED_STRING_CONCAT
                         : (strcmp(method, "copy") == 0
                                ? TYPED_STRING_COPY
                                : TYPED_STRING_FROM_FIXED);
        typed.type_index = dynamic_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = coerced;
        typed.right_child = second;
        typed.auxiliary_type = TYPED_NO_NODE;
        typed.store_id = store->id;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }


    if (strcmp(method, "byte_len") == 0 ||
        strcmp(method, "byte_at") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        bool is_len = strcmp(method, "byte_len") == 0;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != (is_len ? 0u : 1u)) {
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
        if (receiver_type != NULL &&
            receiver_type->kind == NIPPY_TYPE_STRING_LITERAL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
            return false;
        }
        if (receiver_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_TEXT_BUILDER) {
            size_t int32_type;

            if (!is_len) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MEMBER_RECEIVER, method);
                return false;
            }
            if (!builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type)) {
                return false;
            }
            typed.kind = TYPED_BUILDER_LEN;
            typed.type_index = int32_type;
            typed.pos = node->pos;
            typed.name_index = TYPED_NO_NODE;
            typed.left_child = receiver;
            typed.right_child = TYPED_NO_NODE;
            typed.auxiliary_type = TYPED_NO_NODE;
            typed.store_id = TYPED_NO_NODE;
            typed.children_start = 0;
            typed.children_count = 0;
            return emit_node(ctx, &typed, ast_index, &(size_t){0});
        }
        if (receiver_type->kind != NIPPY_TYPE_STRING &&
            receiver_type->kind != NIPPY_TYPE_DYNAMIC_STRING) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (is_len) {
            size_t int32_type;

            if (!builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type)) {
                return false;
            }
            typed.kind = TYPED_STRING_LEN;
            typed.type_index = int32_type;
            typed.pos = node->pos;
            typed.name_index = TYPED_NO_NODE;
            typed.left_child = receiver;
            typed.right_child = TYPED_NO_NODE;
            typed.auxiliary_type = TYPED_NO_NODE;
            typed.store_id = TYPED_NO_NODE;
            typed.children_start = 0;
            typed.children_count = 0;
            return emit_node(ctx, &typed, ast_index, &(size_t){0});
        } else {
            size_t argument_ast = *(const size_t *)list_get(
                &ctx->ast->children, node->children_start);
            size_t index;
            const TypedNode *index_node;
            const TypedType *index_type;
            size_t byte_type;

            if (!check_fixed_index_bounds(ctx, argument_ast,
                                          receiver_node->type_index) ||
                !check_expression(ctx, argument_ast, &index)) {
                return false;
            }
            index_node = typed_program_node(ctx->program, index);
            index_type = index_node == NULL
                             ? NULL
                             : typed_program_type(ctx->program,
                                                  index_node->type_index);
            if (index_type == NULL || index_type->kind != NIPPY_TYPE_INT32) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_STRING_INDEX);
                return false;
            }
            if (!builtin_type(ctx, NIPPY_TYPE_BYTE, &byte_type)) {
                return false;
            }
            typed.kind = TYPED_STRING_AT;
            typed.type_index = byte_type;
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
    }


return true;
}
