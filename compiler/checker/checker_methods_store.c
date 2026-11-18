#include "checker_internal.h"

/* Methods of a Store (alloc, channel) and of the Handle it gives out (read,
   write). */

typedef bool (*CheckStoreMethodHandler)(CheckContext *ctx, const AstNode *node,
                                        const char *method, size_t ast_index);

/* store.alloc<T>(value): the value, coerced to T, copied into the store. */
static bool check_store_alloc(CheckContext *ctx, const AstNode *node,
                              const char *method, size_t ast_index)
{
    StoreEntry store = {0};
    TypedNode typed;
    size_t element;
    size_t value;
    size_t coerced;
    size_t handle;
    char element_text[128];

    if (!member_store(ctx, node, method, &store)) {
        return false;
    }
    if (node->right_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ALLOC_TYPE_ARG);
        return false;
    }
    if (!resolve_type_in_env(ctx, node->right_child, ctx->env, "alloc", &element)) {
        return false;
    }
    if (node->children_count != 1) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ALLOC_ARG_COUNT);
        return false;
    }
    if (!handle_type(ctx, element, &handle) ||
        !check_expression(ctx, *(const size_t *)list_get(&ctx->ast->children,
                                                         node->children_start),
                          &value)) {
        return false;
    }
    if (!inject_into(ctx, value, element, &coerced)) {
        type_ast_text(ctx->ast, node->right_child, element_text,
                      sizeof(element_text));
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ALLOC_ARG_TYPE,
                        element_text);
        return false;
    }
    typed = new_node(TYPED_STORE_ALLOC, handle, node->pos, coerced,
                     TYPED_NO_NODE);
    set_store_reference(ctx, &typed, &store);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* A call on a Handle: the receiver must be a Handle to one type. */
static bool handle_call(ReceiverCall *c, size_t argc, size_t *element)
{
    if (!receiver_prepare(c, argc)) {
        return false;
    }
    if (c->kind != NYX_TYPE_HANDLE || c->child_count != 1) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    *element = *(const size_t *)list_get(&c->ctx->program->type_children,
                                         c->first_child);
    return true;
}

static bool check_store_read(CheckContext *ctx, const AstNode *node,
                             const char *method, size_t ast_index)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    size_t element;
    TypedNode typed;

    if (!handle_call(&c, 0, &element)) {
        return false;
    }
    typed = new_node(TYPED_HANDLE_READ, element, node->pos, c.receiver,
                     TYPED_NO_NODE);
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(&c, &typed);
}

static bool check_store_write(CheckContext *ctx, const AstNode *node,
                              const char *method, size_t ast_index)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    size_t element;
    size_t value;
    size_t coerced;
    size_t nil_type;
    TypedNode typed;

    if (!handle_call(&c, 1, &element) ||
        !check_expression(ctx, receiver_argument(&c, 0), &value)) {
        return false;
    }
    if (!inject_into(ctx, value, element, &coerced)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_WRITE_TYPE);
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_NIL, &nil_type)) {
        return false;
    }
    typed = new_node(TYPED_HANDLE_WRITE, nil_type, node->pos, c.receiver,
                     coerced);
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(&c, &typed);
}

static bool check_channel_creation(CheckContext *ctx, const AstNode *node,
                                   const char *method, size_t ast_index)
{
    bool handled;

    return check_channel_create(ctx, node, method, ast_index, &handled);
}

static const struct {
    const char *key;
    CheckStoreMethodHandler handler;
} check_store_method_handlers[] = {
    {"alloc", check_store_alloc},
    {"channel", check_channel_creation},
    {"read", check_store_read},
    {"write", check_store_write},
};

bool check_store_method(CheckContext *ctx, const AstNode *node,
                        const char *method, size_t ast_index, bool *handled)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(check_store_method_handlers); i++) {
        if (strcmp(check_store_method_handlers[i].key, method) == 0) {
            *handled = true;
            return check_store_method_handlers[i].handler(ctx, node, method,
                                                          ast_index);
        }
    }
    return true;
}
