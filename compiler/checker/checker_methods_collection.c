#include "checker_internal.h"

bool check_collection_method(CheckContext *ctx, const AstNode *node,
    const char *method, size_t ast_index, bool *handled)
{
    const StoreEntry *store = NULL;
    TypedNode typed = {0};

    if (strcmp(method, "len") == 0 ||
        strcmp(method, "has") == 0 ||
        strcmp(method, "remove") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t receiver_name_index;
        bool is_record;
        size_t key_type;
        size_t key = TYPED_NO_NODE;
        size_t child_index = TYPED_NO_NODE;
        size_t bool_type;
        size_t int32_type;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
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
        if (receiver_type == NULL ||
            (receiver_type->kind != NIPPY_TYPE_RECORD &&
             receiver_type->kind != NIPPY_TYPE_TABLE) ||
            receiver_type->child_count != 2) {
            if (strcmp(method, "len") == 0) {
                goto map_len_fallthrough;
            }
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        is_record = receiver_type->kind == NIPPY_TYPE_RECORD;
        key_type = *(const size_t *)list_get(&ctx->program->type_children,
                                             receiver_type->first_child);
        if (strcmp(method, "len") == 0) {
            if (node->children_count != 0) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MEMBER_ARGS, method);
                return false;
            }
        } else {
            size_t key_ast;

            if (node->children_count != 1) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MEMBER_ARGS, method);
                return false;
            }
            key_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                node->children_start);
            if (!check_expression(ctx, key_ast, &key) ||
                !inject_into(ctx, key, key_type, &child_index)) {
                if (!ctx->errors->reported) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MAP_KEY);
                }
                return false;
            }
            if (strcmp(method, "remove") == 0) {
                const AstNode *receiver_ast = ast_get_node(
                    ctx->ast, node->left_child);

                if (receiver_ast == NULL ||
                    receiver_ast->kind != AST_IDENTIFIER) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_LIST_LVALUE);
                    return false;
                }
            }
        }
        if (!builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type) ||
            !builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type)) {
            return false;
        }
        typed.kind = strcmp(method, "len") == 0
                         ? (is_record ? TYPED_RECORD_LEN : TYPED_TABLE_LEN)
                         : (strcmp(method, "has") == 0
                                ? (is_record ? TYPED_RECORD_HAS
                                             : TYPED_TABLE_HAS)
                                : (is_record ? TYPED_RECORD_REMOVE
                                             : TYPED_TABLE_REMOVE));
        typed.type_index = strcmp(method, "len") == 0 ? int32_type : bool_type;
        typed.pos = node->pos;
        typed.name_index = receiver_name_index;
        typed.left_child = receiver;
        typed.right_child = child_index;
        typed.auxiliary_type = key_type;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

map_len_fallthrough:
    if (strcmp(method, "slice") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t element;
        size_t start_ast;
        size_t length_ast;
        size_t start;
        size_t length;
        size_t int32_type;
        size_t coerced_start;
        size_t coerced_length;
        size_t result_type;
        size_t children_start;

        if (node->right_child != AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
            return false;
        }
        if (node->children_count != 2) {
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
        if (receiver_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            size_t dynamic_type;
            size_t start_node;
            size_t length_node;

            start_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                  node->children_start);
            length_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                   node->children_start + 1);
            if (!check_int32_expression(ctx, start_ast, &start_node) ||
                !check_int32_expression(ctx, length_ast, &length_node)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_STRING_SLICE_TYPE);
                return false;
            }
            if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING,
                              &dynamic_type)) {
                return false;
            }
            children_start = ctx->program->children.length;
            if (!typed_program_push_child(ctx->program, start_node) ||
                !typed_program_push_child(ctx->program, length_node)) {
                error_report_fatal(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
                return false;
            }
            typed.kind = TYPED_STRING_SLICE;
            typed.type_index = dynamic_type;
            typed.pos = node->pos;
            typed.name_index = TYPED_NO_NODE;
            typed.left_child = receiver;
            typed.right_child = TYPED_NO_NODE;
            typed.auxiliary_type = TYPED_NO_NODE;
            typed.store_id = TYPED_NO_NODE;
            typed.children_start = children_start;
            typed.children_count = 2;
            return emit_node(ctx, &typed, ast_index, &(size_t){0});
        }
        if (receiver_type->child_count != 1 ||
            (receiver_type->kind != NIPPY_TYPE_LIST &&
             receiver_type->kind != NIPPY_TYPE_SLICE)) {
            error_report_at(ctx->errors, node->pos,
                            receiver_type->kind == NIPPY_TYPE_FIXED_LIST
                                ? MESSAGE_CHECKER_SLICE_DYNAMIC
                                : MESSAGE_CHECKER_MEMBER_RECEIVER,
                            method);
            return false;
        }
        element = *(const size_t *)list_get(
            &ctx->program->type_children, receiver_type->first_child);
        start_ast = *(const size_t *)list_get(&ctx->ast->children,
                                              node->children_start);
        length_ast = *(const size_t *)list_get(&ctx->ast->children,
                                               node->children_start + 1);
        if (!check_expression(ctx, start_ast, &start) ||
            !check_expression(ctx, length_ast, &length) ||
            !builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type) ||
            !inject_into(ctx, start, int32_type, &coerced_start) ||
            !inject_into(ctx, length, int32_type, &coerced_length) ||
            !slice_type(ctx, element, &result_type)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors, MESSAGE_CHECKER_LIST_INDEX);
            }
            return false;
        }
        children_start = ctx->program->children.length;
        if (!typed_program_push_child(ctx->program, coerced_start) ||
            !typed_program_push_child(ctx->program, coerced_length)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            return false;
        }
        typed.kind = TYPED_SLICE_CREATE;
        typed.type_index = result_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.right_child = TYPED_NO_NODE;
        typed.auxiliary_type = element;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = children_start;
        typed.children_count = 2;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "len") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        size_t int32_type;

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
            (receiver_type->kind != NIPPY_TYPE_LIST &&
             receiver_type->kind != NIPPY_TYPE_FIXED_LIST &&
             receiver_type->kind != NIPPY_TYPE_SLICE)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type)) {
            return false;
        }
        typed.kind = receiver_type->kind == NIPPY_TYPE_SLICE
                         ? TYPED_SLICE_LEN
                         : TYPED_LIST_LEN;
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

    if (strcmp(method, "get") == 0) {
        *handled = true;
        size_t receiver;
        size_t argument;
        size_t coerced;
        size_t element;
        size_t nil_type;
        size_t result_type;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;

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
        if (receiver_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        argument = *(const size_t *)list_get(&ctx->ast->children,
                                             node->children_start);
        if (receiver_type->kind == NIPPY_TYPE_TABLE) {
            size_t key_type;
            size_t value_type;
            size_t key;

            if (receiver_type->child_count != 2) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MEMBER_RECEIVER, method);
                return false;
            }
            key_type = *(const size_t *)list_get(
                &ctx->program->type_children, receiver_type->first_child);
            value_type = *(const size_t *)list_get(
                &ctx->program->type_children, receiver_type->first_child + 1);
            if (!check_expression(ctx, argument, &key) ||
                !inject_into(ctx, key, key_type, &coerced)) {
                if (!ctx->errors->reported) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MAP_KEY);
                }
                return false;
            }
            element = value_type;
            typed.kind = TYPED_TABLE_GET_OPTIONAL;
            typed.right_child = coerced;
        } else if (receiver_type->kind == NIPPY_TYPE_LIST ||
                   receiver_type->kind == NIPPY_TYPE_FIXED_LIST ||
                   receiver_type->kind == NIPPY_TYPE_SLICE) {
            size_t index;

            if (receiver_type->child_count != 1) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_MEMBER_RECEIVER, method);
                return false;
            }
            element = *(const size_t *)list_get(
                &ctx->program->type_children, receiver_type->first_child);
            if (!check_fixed_index_bounds(ctx, argument,
                                          receiver_node->type_index)) {
                return false;
            }
            if (!check_int32_expression(ctx, argument, &index)) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_LIST_INDEX);
                return false;
            }
            typed.kind = TYPED_LIST_GET_OPTIONAL;
            typed.right_child = index;
        } else {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            return false;
        }
        if (same_type(ctx->program, element, nil_type)) {
            result_type = element;
        } else {
            List alternatives;

            list_init(&alternatives, sizeof(size_t));
            if (!list_push(&alternatives, &element) ||
                !list_push(&alternatives, &nil_type) ||
                !intern_union(ctx, &alternatives, &result_type)) {
                list_free(&alternatives);
                return false;
            }
            list_free(&alternatives);
        }
        typed.type_index = result_type;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = receiver;
        typed.auxiliary_type = element;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "pop") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        const AstNode *receiver_ast;
        size_t receiver_name_index;
        size_t element;
        size_t nil_type;
        size_t result_type;
        List alternatives;

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
        receiver_ast = ast_get_node(ctx->ast, node->left_child);
        receiver_name_index = receiver_node == NULL
                                  ? TYPED_NO_NODE
                                  : receiver_node->name_index;
        if (receiver_type == NULL ||
            (receiver_type->kind != NIPPY_TYPE_LIST &&
             receiver_type->kind != NIPPY_TYPE_FIXED_LIST) ||
            receiver_type->child_count != 1 ||
            receiver_ast == NULL ||
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
        element = *(const size_t *)list_get(
            &ctx->program->type_children, receiver_type->first_child);
        if (!builtin_type(ctx, NIPPY_TYPE_NIL, &nil_type)) {
            return false;
        }
        if (same_type(ctx->program, element, nil_type)) {
            result_type = element;
        } else {
            list_init(&alternatives, sizeof(size_t));
            if (!list_push(&alternatives, &element) ||
                !list_push(&alternatives, &nil_type) ||
                !intern_union(ctx, &alternatives, &result_type)) {
                list_free(&alternatives);
                return false;
            }
            list_free(&alternatives);
        }
        typed.kind = TYPED_LIST_POP;
        typed.type_index = result_type;
        typed.pos = node->pos;
        typed.name_index = receiver_name_index;
        typed.left_child = receiver;
        typed.right_child = TYPED_NO_NODE;
        typed.auxiliary_type = element;
        typed.store_id = TYPED_NO_NODE;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }


    if (strcmp(method, "list") == 0) {
        *handled = true;
        size_t element;
        size_t list;
        size_t capacity_ast;
        size_t capacity;
        size_t int32_type;
        size_t coerced;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child == AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_TYPE_ARG);
            return false;
        }
        if (!resolve_type_in_env(ctx, node->right_child, ctx->env, "list",
                                 &element)) {
            return false;
        }
        if (!list_type(ctx, element, false, 0, &list)) {
            return false;
        }
        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_CREATE_ARGS);
            return false;
        }
        capacity_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_negative_capacity(ctx, capacity_ast)) {
            return false;
        }
        if (!check_expression(ctx, capacity_ast, &capacity) ||
            !builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type) ||
            !inject_into(ctx, capacity, int32_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_CAPACITY_ARG);
            return false;
        }
        typed.kind = TYPED_LIST_CREATE;
        typed.type_index = list;
        typed.auxiliary_type = element;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = coerced;
        typed.right_child = TYPED_NO_NODE;
        typed.store_id = store->id;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

    if (strcmp(method, "table") == 0) {
        *handled = true;
        const AstNode *type_args;
        size_t key;
        size_t value;
        size_t table;
        size_t capacity_ast;
        size_t capacity;
        size_t int32_type;
        size_t coerced;

        if (!member_store(ctx, node, method, &store)) {
            return false;
        }
        if (node->right_child == AST_NO_NODE) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TABLE_ARGS);
            return false;
        }
        type_args = ast_get_node(ctx->ast, node->right_child);
        if (type_args == NULL || type_args->kind != AST_TYPE_TABLE ||
            !resolve_type_in_env(ctx, type_args->left_child, ctx->env, "table",
                                 &key) ||
            !resolve_type_in_env(ctx, type_args->right_child, ctx->env,
                                 "table", &value)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_TABLE_ARGS);
            return false;
        }
        if (!map_type(ctx, key, value, false, 0, &table)) {
            return false;
        }
        if (node->children_count != 1) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_CREATE_ARGS);
            return false;
        }
        capacity_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start);
        if (!check_negative_capacity(ctx, capacity_ast)) {
            return false;
        }
        if (!check_expression(ctx, capacity_ast, &capacity) ||
            !builtin_type(ctx, NIPPY_TYPE_INT32, &int32_type) ||
            !inject_into(ctx, capacity, int32_type, &coerced)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_LIST_CAPACITY_ARG);
            return false;
        }
        typed.kind = TYPED_TABLE_CREATE;
        typed.type_index = table;
        typed.auxiliary_type = key;
        typed.pos = node->pos;
        typed.name_index = TYPED_NO_NODE;
        typed.left_child = coerced;
        typed.right_child = TYPED_NO_NODE;
        typed.store_id = store->id;
        typed.children_start = 0;
        typed.children_count = 0;
        return emit_node(ctx, &typed, ast_index, &(size_t){0});
    }

return true;
}
