#include "checker_internal.h"

/* Methods of text, text builders and fixed strings, the creation of text in a
   store, and the push of a list (which shares the builder's append syntax). */

static size_t child_type(const ReceiverCall *c, size_t i)
{
    return *(const size_t *)list_get(&c->ctx->program->type_children,
                                     c->first_child + i);
}

/* Check an argument and coerce it to a type made by `make`; `message` is the
   error when it does not fit. */
static bool coerced_argument(const ReceiverCall *c, size_t i, NyxType kind,
                             const char *message, size_t *coerced)
{
    size_t checked;
    size_t type;

    if (!check_expression(c->ctx, receiver_argument(c, i), &checked)) {
        return false;
    }
    if (!builtin_type(c->ctx, kind, &type) ||
        !inject_into(c->ctx, checked, type, coerced)) {
        error_report_at(c->ctx->errors, c->node->pos, message);
        return false;
    }
    return true;
}

/* ---- push, append, append_byte, finish ---- */

/* list.push(value): the list must be a variable, or an array that may be
   changed in place. */
/* The value of a push, coerced to the element type, and the Nil it yields. */
static bool push_value(const ReceiverCall *c, size_t element, size_t *coerced,
                       size_t *nil_type)
{
    CheckContext *ctx = c->ctx;
    size_t value;

    if (check_expression(ctx, receiver_argument(c, 0), &value) &&
        inject_into(ctx, value, element, coerced) &&
        builtin_type(ctx, NYX_TYPE_NIL, nil_type)) {
        return true;
    }
    if (!ctx->errors->reported) {
        error_report(ctx->errors, MESSAGE_CHECKER_LIST_VALUE);
    }
    return false;
}

static bool list_push_call(ReceiverCall *c)
{
    CheckContext *ctx = c->ctx;
    size_t element;
    size_t coerced;
    size_t nil_type;
    TypedNode typed;

    if (!receiver_is_variable(c)) {
        error_report_at(ctx->errors, c->node->pos, MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    if ((c->kind == NYX_TYPE_FIXED_LIST &&
         !check_inline_const_mutation(ctx, c->node->left_child)) ||
        (c->child_count != 1 &&
         !receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER))) {
        return false;
    }
    element = child_type(c, 0);
    if (!push_value(c, element, &coerced, &nil_type)) {
        return false;
    }
    typed = new_node(TYPED_LIST_PUSH, nil_type, c->node->pos, c->receiver,
                     coerced);
    typed.name_index = c->receiver_name;
    typed.auxiliary_type = element;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(c, &typed);
}

/* The operations of a text builder: what each takes and yields. */
static const struct {
    const char *method;
    TypedNodeKind kind;
    NyxType argument;     /* NYX_TYPE_UNKNOWN when it takes none */
    NyxType result;
    const char *message;  /* when the argument does not fit */
} builder_operations[] = {
    {"append", TYPED_BUILDER_APPEND, NYX_TYPE_DYNAMIC_STRING, NYX_TYPE_NIL,
     MESSAGE_CHECKER_BUILDER_APPEND_TYPE},
    {"append_byte", TYPED_BUILDER_APPEND_BYTE, NYX_TYPE_BYTE, NYX_TYPE_NIL,
     MESSAGE_CHECKER_BUILDER_BYTE_TYPE},
    {"finish", TYPED_BUILDER_FINISH, NYX_TYPE_UNKNOWN, NYX_TYPE_DYNAMIC_STRING,
     NULL},
};

static bool builder_call(ReceiverCall *c, size_t operation)
{
    NyxType argument_kind = builder_operations[operation].argument;
    size_t argument = TYPED_NO_NODE;
    size_t result_type;
    TypedNode typed;

    if (c->kind != NYX_TYPE_TEXT_BUILDER) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    if ((argument_kind != NYX_TYPE_UNKNOWN &&
         !coerced_argument(c, 0, argument_kind,
                           builder_operations[operation].message, &argument)) ||
        !builtin_type(c->ctx, builder_operations[operation].result,
                      &result_type)) {
        return false;
    }
    typed = new_node(builder_operations[operation].kind, result_type,
                     c->node->pos, c->receiver, argument);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(c, &typed);
}

static bool check_push_or_builder(CheckContext *ctx, const AstNode *node,
                                  const char *method, size_t ast_index)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    bool push = strcmp(method, "push") == 0;
    size_t operation = 0;

    while (!push && strcmp(method, builder_operations[operation].method) != 0) {
        operation++;
    }
    if (!receiver_prepare(&c, strcmp(method, "finish") == 0 ? 0u : 1u)) {
        return false;
    }
    if (push) {
        return (c.kind == NYX_TYPE_LIST || c.kind == NYX_TYPE_FIXED_LIST)
                   ? list_push_call(&c)
                   : receiver_reject(&c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    return builder_call(&c, operation);
}

/* ---- text made in a store ---- */

/* store.builder(capacity). */
static bool check_builder_creation(CheckContext *ctx, const AstNode *node,
                                   const char *method, size_t ast_index)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    StoreEntry store = {0};
    size_t capacity_ast;
    size_t capacity;
    size_t int32_type;
    size_t coerced;
    size_t builder_type;
    TypedNode typed;

    if (!member_store(ctx, node, method, &store)) {
        return false;
    }
    if (node->right_child != AST_NO_NODE) {
        return receiver_reject(&c, MESSAGE_CHECKER_MEMBER_TYPE_ARG);
    }
    if (node->children_count != 1) {
        return receiver_reject(&c, MESSAGE_CHECKER_MEMBER_ARGS);
    }
    capacity_ast = receiver_argument(&c, 0);
    if (!check_negative_capacity(ctx, capacity_ast) ||
        !check_expression(ctx, capacity_ast, &capacity)) {
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_INT32, &int32_type) ||
        !inject_into(ctx, capacity, int32_type, &coerced)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_BUILDER_CAPACITY);
        return false;
    }
    if (!text_builder_type(ctx, &builder_type)) {
        return false;
    }
    typed = new_node(TYPED_BUILDER_CREATE, builder_type, node->pos, coerced,
                     TYPED_NO_NODE);
    typed.auxiliary_type = TYPED_NO_NODE;
    set_store_reference(ctx, &typed, &store);
    return receiver_emit(&c, &typed);
}

/* The error a concat/copy/text argument gets when it is not text. */
static const char *text_argument_message(const char *method)
{
    return strcmp(method, "concat") == 0 ? MESSAGE_CHECKER_CONCAT_TYPE
                                         : MESSAGE_CHECKER_COPY_TYPE;
}

/* The first argument of concat, copy and text: a string value, which text
   takes as it is and the others coerce to Text. */
static bool text_source(const ReceiverCall *c, size_t dynamic_type,
                        size_t *coerced)
{
    CheckContext *ctx = c->ctx;
    const TypedNode *node;
    const TypedType *type;
    size_t argument;

    if (!check_expression(ctx, receiver_argument(c, 0), &argument)) {
        return false;
    }
    node = typed_program_node(ctx->program, argument);
    type = node == NULL ? NULL
                        : typed_program_type(ctx->program, node->type_index);
    if (type == NULL) {
        error_report_at(ctx->errors, c->node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (strcmp(c->method, "text") == 0) {
        *coerced = argument;
        if (type->kind != NYX_TYPE_STRING) {
            error_report_at(ctx->errors, c->node->pos,
                            MESSAGE_CHECKER_STORE_TEXT_TYPE);
            return false;
        }
        return true;
    }
    if (!inject_into(ctx, argument, dynamic_type, coerced)) {
        error_report_at(ctx->errors, c->node->pos,
                        text_argument_message(c->method));
        return false;
    }
    return true;
}

/* The second argument of concat. */
static bool text_second(const ReceiverCall *c, size_t dynamic_type,
                        size_t *second)
{
    size_t argument;

    if (!check_expression(c->ctx, receiver_argument(c, 1), &argument)) {
        return false;
    }
    if (!inject_into(c->ctx, argument, dynamic_type, second)) {
        error_report_at(c->ctx->errors, c->node->pos,
                        MESSAGE_CHECKER_CONCAT_TYPE);
        return false;
    }
    return true;
}

/* The text arguments of concat, copy and text, as Text. */
static bool text_arguments(const ReceiverCall *c, bool concat, size_t dynamic_type,
                           size_t *first, size_t *second)
{
    *second = TYPED_NO_NODE;
    return text_source(c, dynamic_type, first) &&
           (!concat || text_second(c, dynamic_type, second));
}

/* store.concat(a, b), store.copy(a), store.text(fixed). */
static bool check_text_creation(CheckContext *ctx, const AstNode *node,
                                const char *method, size_t ast_index)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    bool concat = strcmp(method, "concat") == 0;
    StoreEntry store = {0};
    size_t dynamic_type;
    size_t coerced;
    size_t second;
    TypedNode typed;

    if (!member_store(ctx, node, method, &store)) {
        return false;
    }
    if (node->right_child != AST_NO_NODE) {
        return receiver_reject(&c, MESSAGE_CHECKER_MEMBER_TYPE_ARG);
    }
    if (node->children_count != (concat ? 2u : 1u)) {
        return receiver_reject(&c, MESSAGE_CHECKER_MEMBER_ARGS);
    }
    if (!builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &dynamic_type) ||
        !text_arguments(&c, concat, dynamic_type, &coerced, &second)) {
        return false;
    }
    typed = new_node(concat ? TYPED_STRING_CONCAT
                            : strcmp(method, "copy") == 0 ? TYPED_STRING_COPY
                                                          : TYPED_STRING_FROM_FIXED,
                     dynamic_type, node->pos, coerced, second);
    typed.auxiliary_type = TYPED_NO_NODE;
    set_store_reference(ctx, &typed, &store);
    return receiver_emit(&c, &typed);
}

/* ---- byte_len, byte_at ---- */

/* The index of byte_at, which must be an Int32 within a fixed string's bounds. */
static bool byte_index(const ReceiverCall *c, size_t *index)
{
    size_t argument = receiver_argument(c, 0);

    if (!check_fixed_index_bounds(c->ctx, argument, c->type_index)) {
        return false;
    }
    if (!check_int32_expression(c->ctx, argument, index)) {
        error_report_at(c->ctx->errors, c->node->pos,
                        MESSAGE_CHECKER_STRING_INDEX);
        return false;
    }
    return true;
}

/* Whether byte_len / byte_at applies to the receiver: byte_len to a builder, a
   string or Text; byte_at to a string or Text. */
static bool byte_receiver_ok(const ReceiverCall *c, bool is_len)
{
    bool text = c->kind == NYX_TYPE_STRING || c->kind == NYX_TYPE_DYNAMIC_STRING;

    return text || (is_len && c->kind == NYX_TYPE_TEXT_BUILDER);
}

/* byte_len and byte_at do not apply to a literal (it has no type yet) or to a
   receiver that is not text. */
static bool check_byte_receiver(const ReceiverCall *c, bool is_len)
{
    const TypedType *literal =
        typed_program_type(c->ctx->program, c->type_index);

    if (literal != NULL && literal->kind == NYX_TYPE_STRING_LITERAL) {
        error_report_at(c->ctx->errors, c->node->pos,
                        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
        return false;
    }
    return byte_receiver_ok(c, is_len) ||
           receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
}

static TypedNodeKind byte_node_kind(const ReceiverCall *c, bool is_len)
{
    if (c->kind == NYX_TYPE_TEXT_BUILDER) {
        return TYPED_BUILDER_LEN;
    }
    return is_len ? TYPED_STRING_LEN : TYPED_STRING_AT;
}

static bool check_byte_len_or_at(CheckContext *ctx, const AstNode *node,
                                 const char *method, size_t ast_index)
{
    ReceiverCall c = {.ctx = ctx, .node = node, .method = method,
                      .ast_index = ast_index};
    bool is_len = strcmp(method, "byte_len") == 0;
    size_t result_type;
    size_t index = TYPED_NO_NODE;
    TypedNode typed;

    if (!receiver_prepare(&c, is_len ? 0u : 1u) ||
        !check_byte_receiver(&c, is_len) ||
        (!is_len && !byte_index(&c, &index)) ||
        !builtin_type(ctx, is_len ? NYX_TYPE_INT32 : NYX_TYPE_BYTE, &result_type)) {
        return false;
    }
    typed = new_node(byte_node_kind(&c, is_len), result_type, node->pos,
                     c.receiver, index);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(&c, &typed);
}

/* ---- dispatch ---- */

static const struct {
    const char *method;
    bool (*check)(CheckContext *ctx, const AstNode *node, const char *method,
                  size_t ast_index);
} string_methods[] = {
    {"append", check_push_or_builder},
    {"push", check_push_or_builder},
    {"append_byte", check_push_or_builder},
    {"finish", check_push_or_builder},
    {"builder", check_builder_creation},
    {"concat", check_text_creation},
    {"copy", check_text_creation},
    {"text", check_text_creation},
    {"byte_len", check_byte_len_or_at},
    {"byte_at", check_byte_len_or_at},
};

bool check_string_method(CheckContext *ctx, const AstNode *node,
                         const char *method, size_t ast_index, bool *handled)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(string_methods); i++) {
        if (strcmp(string_methods[i].method, method) == 0) {
            *handled = true;
            return string_methods[i].check(ctx, node, method, ast_index);
        }
    }
    return true;
}
