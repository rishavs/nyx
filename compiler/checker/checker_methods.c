#include "checker_internal.h"

bool member_store(CheckContext *ctx, const AstNode *node,
                         const char *method, StoreEntry *out_store)
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
    {
        const StoreEntry *store = find_store(ctx->stores, name);
        if (store != NULL) {
            *out_store = *store;
            return true;
        }
    }
    {
        const VariableEntry *variable = find_variable(ctx->locals, name);
        const TypedType *type = variable == NULL ? NULL :
                                typed_program_type(ctx->program, variable->type_index);
        if (type != NULL && type->kind == NIPPY_TYPE_STORE) {
            memset(out_store, 0, sizeof(*out_store));
            memcpy(out_store->name, name, strlen(name) + 1);
            out_store->id = TYPED_NO_NODE;
            return true;
        }
    }
    error_report_at(ctx->errors, receiver->pos,
                    MESSAGE_CHECKER_UNKNOWN_STORE, name);
    return false;
}

bool set_store_reference(CheckContext *ctx, TypedNode *node,
                         const StoreEntry *store)
{
    node->store_id = store->id;
    if (store->id != TYPED_NO_NODE) return true;
    node->name_index = typed_program_copy_symbol(ctx->program, store->name,
                                                  strlen(store->name));
    if (node->name_index == TYPED_NO_NODE) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    return true;
}



static bool check_member_call_variable(const VariableEntry *variable, CheckContext *ctx, const char *receiver_name, size_t *receiver_type_index)
{
    if (variable == NULL && ctx->in_function &&
        receiver_name != NULL &&
        find_variable(ctx->globals, receiver_name) != NULL) {
        return false;
    }
    if (variable != NULL) {
        (*receiver_type_index) = variable->type_index;
        if (typed_program_type(ctx->program, (*receiver_type_index)) !=
            NULL &&
            typed_program_type(ctx->program,
                               (*receiver_type_index))->kind ==
                NIPPY_TYPE_UNION) {
            size_t i;

            for (i = ctx->narrowings->length; i > 0; i--) {
                const Narrowing *narrowing = list_get(
                    ctx->narrowings, i - 1);

                if (narrowing != NULL && narrowing->active &&
                    narrowing->path_ast == AST_NO_NODE &&
                    strcmp(narrowing->name, receiver_name) == 0) {
                    (*receiver_type_index) = narrowing->type_index;
                    break;
                }
            }
        }
    }
    return true;
}

static bool check_member_call_receiver_name(const AstNode *receiver_ast, CheckContext *ctx, const AstNode *node, size_t *receiver_type_index, bool *outcome)
{
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
        if (!check_member_call_variable(variable, ctx, receiver_name, receiver_type_index)) {
    do { *outcome = (check_expression(ctx, node->left_child,
                                    &(size_t){0})); return true; } while (0);
}
    } else {
        size_t receiver;
        const TypedNode *receiver_node;

        if (!check_expression(ctx, node->left_child, &receiver)) {
            do { *outcome = (false); return true; } while (0);
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        if (receiver_node != NULL) {
            (*receiver_type_index) = receiver_node->type_index;
        }
    }
    return false;
}

static bool check_member_call_step_step(const char *method, CheckContext *ctx, const AstNode *node, bool *outcome2, size_t ast_index)
{
    if (method == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        do { *outcome2 = (false); return false; } while (0);
    }
    {
        const AstNode *receiver_ast = ast_get_node(ctx->ast,
                                                  node->left_child);
        size_t receiver_type_index = TYPED_NO_NODE;
        const TypedType *receiver_type;

        {
        bool outcome;

        if (check_member_call_receiver_name(receiver_ast, ctx, node, &receiver_type_index, &outcome)) {
            do { *outcome2 = (outcome); return false; } while (0);
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
                    do { *outcome2 = (false); return false; } while (0);
                }
                if (function->generic) {
                    size_t index = find_function_index(ctx,
                                                       function->name);

                    do { *outcome2 = (check_generic_call(ctx, node, index, ast_index)); return false; } while (0);
                }
                do { *outcome2 = (check_associated_function_call(
                    ctx, node, function, ast_index, true)); return false; } while (0);
            }
        }
    }
    return true;
}

static bool check_member_call_step_member_call_step_step(const char *method, CheckContext *ctx, const AstNode *node, bool *outcome2, size_t ast_index, bool *handled)
{
    if (!check_member_call_step_step(method, ctx, node, outcome2, ast_index)) {
        return false;
    }

    if (!check_collection_method(ctx, node, method, ast_index, handled)) {
        do { *outcome2 = (false); return false; } while (0);
    }
    if ((*handled)) {
        do { *outcome2 = (true); return false; } while (0);
    }
    if (!check_string_method(ctx, node, method, ast_index, handled)) {
        do { *outcome2 = (false); return false; } while (0);
    }
    if ((*handled)) {
        do { *outcome2 = (true); return false; } while (0);
    }
    if (!check_error_method(ctx, node, method, ast_index, handled)) {
        do { *outcome2 = (false); return false; } while (0);
    }
    if ((*handled)) {
        do { *outcome2 = (true); return false; } while (0);
    }
    return true;
}

static bool check_member_call_step(const char *method, CheckContext *ctx, const AstNode *node, size_t ast_index, bool *handled, bool *outcome2)
{
    if (!check_member_call_step_member_call_step_step(method, ctx, node, outcome2, ast_index, handled)) {
        return true;
    }
    return false;
}

bool check_member_call(CheckContext *ctx, const AstNode *node,
                      size_t ast_index)
{
    const char *method = ast_symbol_text(ctx->ast, node->name_index);
    bool handled = false;

    {
        bool outcome2;

        if (check_member_call_step(method, ctx, node, ast_index, &handled, &outcome2)) {
            return outcome2;
        }
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
