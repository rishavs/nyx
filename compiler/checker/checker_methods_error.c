#include "checker_internal.h"

bool check_error_method(CheckContext *ctx, const AstNode *node,
    const char *method, size_t ast_index, bool *handled)
{
    TypedNode typed = {0};

    if (strcmp(method, "header") == 0 ||
        strcmp(method, "description") == 0 ||
        strcmp(method, "filename") == 0 ||
        strcmp(method, "line") == 0 ||
        strcmp(method, "col") == 0 ||
        strcmp(method, "pos") == 0) {
        *handled = true;
        size_t receiver;
        const TypedNode *receiver_node;
        const TypedType *receiver_type;
        static const char *const names[] = {
            "header", "description", "filename", "line", "col", "pos"
        };
        size_t field = 0;
        size_t field_type;
        size_t i;

        for (i = 0; i < 6; i++) {
            if (strcmp(method, names[i]) == 0) {
                field = i;
                break;
            }
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
        if (!check_expression(ctx, node->left_child, &receiver)) {
            return false;
        }
        receiver_node = typed_program_node(ctx->program, receiver);
        receiver_type = receiver_node == NULL
                            ? NULL
                            : typed_program_type(ctx->program,
                                                 receiver_node->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_ERROR) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MEMBER_RECEIVER, method);
            return false;
        }
        if (!error_field_type(ctx, receiver_node->type_index, field,
                              &field_type)) {
            return false;
        }
        typed.kind = TYPED_ERROR_FIELD;
        typed.type_index = field_type;
        typed.integer_value = (int)field;
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


return true;
}
