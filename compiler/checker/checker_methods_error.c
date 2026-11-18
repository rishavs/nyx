#include "checker_internal.h"

/* The fields of a caught Error read as methods: error.line(). */
static const char *const error_field_names[] = {
    "header", "description", "filename", "line", "col", "pos",
};

bool check_error_method(CheckContext *ctx, const AstNode *node,
                        const char *method, size_t ast_index, bool *handled)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    size_t field = 0;
    size_t field_type;
    TypedNode typed;

    while (field < NYX_COUNT_OF(error_field_names) &&
           strcmp(method, error_field_names[field]) != 0) {
        field++;
    }
    if (field == NYX_COUNT_OF(error_field_names)) {
        return true;
    }
    *handled = true;
    if (!receiver_prepare(&c, 0)) {
        return false;
    }
    if (c.kind != NYX_TYPE_ERROR) {
        return receiver_reject(&c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    if (!error_field_type(ctx, c.type_index, field, &field_type)) {
        return false;
    }
    typed = new_node(TYPED_ERROR_FIELD, field_type, node->pos, c.receiver,
                     TYPED_NO_NODE);
    typed.integer_value = (int)field;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(&c, &typed);
}
