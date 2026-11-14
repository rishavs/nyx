#include "checker_internal.h"

bool member_store(CheckContext *ctx, const AstNode *node,
                         const char *method, const StoreEntry **out_store)
{
    const AstNode *receiver = ast_get_node(ctx->ast, node->left_child);
    const char *name;

    if (receiver == NULL || receiver->kind != AST_IDENTIFIER) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MEMBER_RECEIVER, method);
        return false;
    }
    name = ast_symbol_text(ctx->ast, receiver->name_index);
    if (name == NULL) {
        error_report_at(ctx->errors, receiver->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    *out_store = find_store(ctx->stores, name);
    if (*out_store == NULL) {
        error_report_at(ctx->errors, receiver->pos,
                        MESSAGE_CHECKER_UNKNOWN_STORE, name);
        return false;
    }
    return true;
}



bool check_member_call(CheckContext *ctx, const AstNode *node,
                      size_t ast_index)
{
    const char *method = ast_symbol_text(ctx->ast, node->name_index);
    bool handled = false;

    if (method == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    {
        const AstNode *receiver_ast = ast_get_node(ctx->ast,
                                                  node->left_child);
        size_t receiver_type_index = TYPED_NO_NODE;
        const TypedType *receiver_type;

        if (receiver_ast != NULL &&
            receiver_ast->kind == AST_IDENTIFIER) {
            const char *receiver_name = ast_symbol_text(
                ctx->ast, receiver_ast->name_index);
            const VariableEntry *variable =
                receiver_name == NULL
                    ? NULL
                    : find_variable(ctx->locals, receiver_name);

            if (variable == NULL && !ctx->in_function &&
                receiver_name != NULL) {
                variable = find_variable(ctx->globals, receiver_name);
            }
            if (variable == NULL && ctx->in_function &&
                receiver_name != NULL &&
                find_variable(ctx->globals, receiver_name) != NULL) {
                return check_expression(ctx, node->left_child,
                                        &(size_t){0});
            }
            if (variable != NULL) {
                receiver_type_index = variable->type_index;
                if (typed_program_type(ctx->program, receiver_type_index) !=
                    NULL &&
                    typed_program_type(ctx->program,
                                       receiver_type_index)->kind ==
                        NIPPY_TYPE_UNION) {
                    size_t i;

                    for (i = ctx->narrowings->length; i > 0; i--) {
                        const Narrowing *narrowing = list_get(
                            ctx->narrowings, i - 1);

                        if (narrowing != NULL && narrowing->active &&
                            narrowing->path_ast == AST_NO_NODE &&
                            strcmp(narrowing->name, receiver_name) == 0) {
                            receiver_type_index = narrowing->type_index;
                            break;
                        }
                    }
                }
            }
        } else {
            size_t receiver;
            const TypedNode *receiver_node;

            if (!check_expression(ctx, node->left_child, &receiver)) {
                return false;
            }
            receiver_node = typed_program_node(ctx->program, receiver);
            if (receiver_node != NULL) {
                receiver_type_index = receiver_node->type_index;
            }
        }
        receiver_type = typed_program_type(ctx->program,
                                           receiver_type_index);
        if (receiver_type != NULL &&
            receiver_type->kind == NIPPY_TYPE_STRUCT) {
            const char *owner = typed_program_symbol_text(
                ctx->program, receiver_type->source_name_index);
            const FunctionEntry *function = find_associated_function(
                ctx->functions, owner, method);

            if (function != NULL && function->is_method) {
                if (node->right_child != AST_NO_NODE) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
                    return false;
                }
                if (function->generic) {
                    size_t index = find_function_index(ctx,
                                                       function->name);

                    return check_generic_call(ctx, node, index, ast_index);
                }
                return check_associated_function_call(
                    ctx, node, function, ast_index, true);
            }
        }
    }

    if (!check_collection_method(ctx, node, method, ast_index, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    if (!check_string_method(ctx, node, method, ast_index, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    if (!check_error_method(ctx, node, method, ast_index, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }
    if (!check_store_method(ctx, node, method, ast_index, &handled)) {
        return false;
    }
    if (handled) {
        return true;
    }

    if (node->right_child != AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MEMBER_TYPE_ARG, method);
        return false;
    }

    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_MEMBER,
                    method);
    return false;
}
