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
        const AstNode *receiver = ast_get_node(ctx->ast, node->left_child);
        const char *receiver_name =
            receiver == NULL || receiver->kind != AST_IDENTIFIER
                ? NULL
                : ast_symbol_text(ctx->ast, receiver->name_index);

        if (strcmp(method, "throw") == 0 && receiver_name != NULL &&
            strcmp(receiver_name, "Error") == 0) {
            return check_error_throw(ctx, node, ast_index);
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

    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_MEMBER,
                    method);
    return false;
}
