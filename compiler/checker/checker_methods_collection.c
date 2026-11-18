#include "checker_internal.h"

/* Methods of lists, arrays, slices, records, tables and text: len, has,
   remove, slice, get, pop, and the creation of a list or table in a store. */

static bool is_list_like(NyxType kind)
{
    return kind == NYX_TYPE_LIST || kind == NYX_TYPE_FIXED_LIST ||
           kind == NYX_TYPE_SLICE;
}

static bool is_map_like(const ReceiverCall *c)
{
    return (c->kind == NYX_TYPE_RECORD || c->kind == NYX_TYPE_TABLE) &&
           c->child_count == 2;
}

static size_t type_part(const ReceiverCall *c, size_t i)
{
    return *(const size_t *)list_get(&c->ctx->program->type_children,
                                     c->first_child + i);
}

/* ---- len, has, remove ---- */

static bool list_len(ReceiverCall *c)
{
    size_t int32_type;
    TypedNode typed;

    if (!is_list_like(c->kind)) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    typed = new_node(c->kind == NYX_TYPE_SLICE ? TYPED_SLICE_LEN : TYPED_LIST_LEN,
                     TYPED_NO_NODE, c->node->pos, c->receiver, TYPED_NO_NODE);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    if (!builtin_type(c->ctx, NYX_TYPE_INT32, &int32_type)) {
        return false;
    }
    typed.type_index = int32_type;
    return receiver_emit(c, &typed);
}

/* The kind of record or table operation for a method name. */
static TypedNodeKind map_operation(const ReceiverCall *c, bool *yields_bool)
{
    bool record = c->kind == NYX_TYPE_RECORD;

    *yields_bool = strcmp(c->method, "len") != 0;
    if (!*yields_bool) {
        return record ? TYPED_RECORD_LEN : TYPED_TABLE_LEN;
    }
    if (strcmp(c->method, "has") == 0) {
        return record ? TYPED_RECORD_HAS : TYPED_TABLE_HAS;
    }
    return record ? TYPED_RECORD_REMOVE : TYPED_TABLE_REMOVE;
}

/* The key of has/remove, coerced to the map's key type; remove also needs the
   map to be a variable it can change. */
static bool map_key(const ReceiverCall *c, size_t key_type, size_t *coerced)
{
    CheckContext *ctx = c->ctx;
    const AstNode *receiver = ast_get_node(ctx->ast, c->node->left_child);
    size_t key;

    if (c->node->children_count != 1) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_ARGS);
    }
    if (!check_expression(ctx, receiver_argument(c, 0), &key) ||
        !inject_into(ctx, key, key_type, coerced)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, c->node->pos, MESSAGE_CHECKER_MAP_KEY);
        }
        return false;
    }
    if (strcmp(c->method, "remove") == 0 &&
        (receiver == NULL || receiver->kind != AST_IDENTIFIER)) {
        error_report_at(ctx->errors, c->node->pos, MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    return true;
}

static bool map_member(ReceiverCall *c)
{
    size_t key_type;
    size_t child_index = TYPED_NO_NODE;
    size_t bool_type;
    size_t int32_type;
    bool yields_bool;
    TypedNode typed;
    TypedNodeKind kind;

    if (!is_map_like(c)) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    key_type = type_part(c, 0);
    kind = map_operation(c, &yields_bool);
    if (yields_bool && !map_key(c, key_type, &child_index)) {
        return false;
    }
    if (!builtin_type(c->ctx, NYX_TYPE_BOOL, &bool_type) ||
        !builtin_type(c->ctx, NYX_TYPE_INT32, &int32_type)) {
        return false;
    }
    typed = new_node(kind, yields_bool ? bool_type : int32_type, c->node->pos,
                     c->receiver, child_index);
    typed.name_index = c->receiver_name;
    typed.auxiliary_type = key_type;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(c, &typed);
}

/* len works on a map or a list; has and remove only on a map. */
static bool check_len(ReceiverCall *c)
{
    if (!receiver_prepare(c, 0)) {
        return false;
    }
    return is_map_like(c) ? map_member(c) : list_len(c);
}

static bool check_has_or_remove(ReceiverCall *c)
{
    if (c->node->right_child != AST_NO_NODE) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_TYPE_ARG);
    }
    return receiver_check(c) && map_member(c);
}

/* ---- slice ---- */

static bool string_slice(ReceiverCall *c)
{
    CheckContext *ctx = c->ctx;
    size_t start;
    size_t length;
    size_t dynamic_type;
    TypedNode typed;

    if (!check_int32_expression(ctx, receiver_argument(c, 0), &start) ||
        !check_int32_expression(ctx, receiver_argument(c, 1), &length)) {
        error_report_at(ctx->errors, c->node->pos,
                        MESSAGE_CHECKER_STRING_SLICE_TYPE);
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &dynamic_type)) {
        return false;
    }
    typed = new_node(TYPED_STRING_SLICE, dynamic_type, c->node->pos,
                     c->receiver, TYPED_NO_NODE);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 2;
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, start),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, length),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    return receiver_emit(c, &typed);
}

/* The start and length of a slice, as Int32s. */
static bool slice_arguments(const ReceiverCall *c, size_t element,
                            size_t *start, size_t *length, size_t *result_type)
{
    CheckContext *ctx = c->ctx;
    size_t raw_start;
    size_t raw_length;
    size_t int32_type;

    if (!check_expression(ctx, receiver_argument(c, 0), &raw_start) ||
        !check_expression(ctx, receiver_argument(c, 1), &raw_length) ||
        !builtin_type(ctx, NYX_TYPE_INT32, &int32_type) ||
        !inject_into(ctx, raw_start, int32_type, start) ||
        !inject_into(ctx, raw_length, int32_type, length) ||
        !slice_type(ctx, element, result_type)) {
        if (!ctx->errors->reported) {
            error_report(ctx->errors, MESSAGE_CHECKER_LIST_INDEX);
        }
        return false;
    }
    return true;
}

static bool list_slice(ReceiverCall *c)
{
    CheckContext *ctx = c->ctx;
    size_t element;
    size_t start;
    size_t length;
    size_t result_type;
    TypedNode typed;

    if (c->child_count != 1 ||
        (c->kind != NYX_TYPE_LIST && c->kind != NYX_TYPE_SLICE)) {
        return receiver_reject(c, c->kind == NYX_TYPE_FIXED_LIST
                                      ? MESSAGE_CHECKER_SLICE_DYNAMIC
                                      : MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    element = type_part(c, 0);
    if (!slice_arguments(c, element, &start, &length, &result_type)) {
        return false;
    }
    typed = new_node(TYPED_SLICE_CREATE, result_type, c->node->pos, c->receiver,
                     TYPED_NO_NODE);
    typed.auxiliary_type = element;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 2;
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, start),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, length),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    return receiver_emit(c, &typed);
}

static bool check_slice(ReceiverCall *c)
{
    if (!receiver_prepare(c, 2)) {
        return false;
    }
    if (c->kind == NYX_TYPE_UNKNOWN) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    return c->kind == NYX_TYPE_DYNAMIC_STRING ? string_slice(c) : list_slice(c);
}

/* ---- get, pop ---- */

/* get(key) on a table. */
static bool table_get_key(const ReceiverCall *c, size_t *coerced, size_t *element)
{
    CheckContext *ctx = c->ctx;
    size_t key;

    if (c->child_count != 2) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    *element = type_part(c, 1);
    if (!check_expression(ctx, receiver_argument(c, 0), &key) ||
        !inject_into(ctx, key, type_part(c, 0), coerced)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, c->node->pos, MESSAGE_CHECKER_MAP_KEY);
        }
        return false;
    }
    return true;
}

/* get(index) on a list, array or slice. */
static bool list_get_index(const ReceiverCall *c, size_t *index, size_t *element)
{
    CheckContext *ctx = c->ctx;

    if (c->child_count != 1) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    *element = type_part(c, 0);
    if (!check_fixed_index_bounds(ctx, receiver_argument(c, 0), c->type_index)) {
        return false;
    }
    if (!check_int32_expression(ctx, receiver_argument(c, 0), index)) {
        error_report_at(ctx->errors, c->node->pos, MESSAGE_CHECKER_LIST_INDEX);
        return false;
    }
    return true;
}

static bool check_get(ReceiverCall *c)
{
    size_t argument;
    size_t element = TYPED_NO_NODE;
    size_t result_type;
    TypedNode typed;
    bool table;

    if (!receiver_prepare(c, 1)) {
        return false;
    }
    table = c->kind == NYX_TYPE_TABLE;
    if (!table && !is_list_like(c->kind)) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_RECEIVER);
    }
    if (!(table ? table_get_key(c, &argument, &element)
                : list_get_index(c, &argument, &element)) ||
        !optional_type(c->ctx, element, &result_type)) {
        return false;
    }
    typed = new_node(table ? TYPED_TABLE_GET_OPTIONAL : TYPED_LIST_GET_OPTIONAL,
                     result_type, c->node->pos, c->receiver, argument);
    typed.auxiliary_type = element;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(c, &typed);
}

/* pop works on a list or array held in a variable. */
/* pop works on a list or array held in a variable. */
static bool pop_receiver_ok(const ReceiverCall *c)
{
    bool list = c->kind == NYX_TYPE_LIST || c->kind == NYX_TYPE_FIXED_LIST;

    return list && c->child_count == 1 && receiver_is_variable(c);
}

static bool check_pop(ReceiverCall *c)
{
    size_t element;
    size_t result_type;
    TypedNode typed;

    if (!receiver_prepare(c, 0)) {
        return false;
    }
    if (!pop_receiver_ok(c)) {
        error_report_at(c->ctx->errors, c->node->pos, MESSAGE_CHECKER_LIST_LVALUE);
        return false;
    }
    element = type_part(c, 0);
    if ((c->kind == NYX_TYPE_FIXED_LIST &&
         !check_inline_const_mutation(c->ctx, c->node->left_child)) ||
        !optional_type(c->ctx, element, &result_type)) {
        return false;
    }
    typed = new_node(TYPED_LIST_POP, result_type, c->node->pos, c->receiver,
                     TYPED_NO_NODE);
    typed.name_index = c->receiver_name;
    typed.auxiliary_type = element;
    typed.store_id = TYPED_NO_NODE;
    return receiver_emit(c, &typed);
}

/* ---- creating a list or table in a store ---- */

/* store.list<T>(capacity) and store.table<K, V>(capacity). */
static bool creation_capacity(const ReceiverCall *c, size_t *coerced)
{
    CheckContext *ctx = c->ctx;
    size_t capacity_ast;
    size_t capacity;
    size_t int32_type;

    if (c->node->children_count != 1) {
        return receiver_reject(c, MESSAGE_CHECKER_LIST_CREATE_ARGS);
    }
    capacity_ast = receiver_argument(c, 0);
    if (!check_negative_capacity(ctx, capacity_ast)) {
        return false;
    }
    if (!check_expression(ctx, capacity_ast, &capacity) ||
        !builtin_type(ctx, NYX_TYPE_INT32, &int32_type) ||
        !inject_into(ctx, capacity, int32_type, coerced)) {
        error_report_at(ctx->errors, c->node->pos,
                        MESSAGE_CHECKER_LIST_CAPACITY_ARG);
        return false;
    }
    return true;
}

static bool emit_creation(const ReceiverCall *c, TypedNodeKind kind, size_t type,
                          size_t auxiliary, size_t capacity,
                          const StoreEntry *store)
{
    TypedNode typed = new_node(kind, type, c->node->pos, capacity, TYPED_NO_NODE);

    typed.auxiliary_type = auxiliary;
    set_store_reference(c->ctx, &typed, store);
    return receiver_emit(c, &typed);
}

static bool check_list_creation(ReceiverCall *c)
{
    StoreEntry store = {0};
    size_t element;
    size_t list;
    size_t capacity;

    if (!member_store(c->ctx, c->node, c->method, &store)) {
        return false;
    }
    if (c->node->right_child == AST_NO_NODE) {
        return receiver_reject(c, MESSAGE_CHECKER_LIST_TYPE_ARG);
    }
    if (!resolve_type_in_env(c->ctx, c->node->right_child, c->ctx->env, "list",
                             &element) ||
        !list_type(c->ctx, element, false, 0, &list)) {
        return false;
    }
    if (!creation_capacity(c, &capacity)) {
        return false;
    }
    return emit_creation(c, TYPED_LIST_CREATE, list, element, capacity, &store);
}

static bool check_table_creation(ReceiverCall *c)
{
    CheckContext *ctx = c->ctx;
    const AstNode *type_args;
    StoreEntry store = {0};
    size_t key;
    size_t value;
    size_t table;
    size_t capacity;

    if (!member_store(ctx, c->node, c->method, &store)) {
        return false;
    }
    type_args = c->node->right_child == AST_NO_NODE
                    ? NULL
                    : ast_get_node(ctx->ast, c->node->right_child);
    if (type_args == NULL || type_args->kind != AST_TYPE_TABLE ||
        !resolve_type_in_env(ctx, type_args->left_child, ctx->env, "table",
                             &key) ||
        !resolve_type_in_env(ctx, type_args->right_child, ctx->env, "table",
                             &value)) {
        return receiver_reject(c, MESSAGE_CHECKER_TABLE_ARGS);
    }
    if (!map_type(ctx, key, value, false, 0, &table) ||
        !creation_capacity(c, &capacity)) {
        return false;
    }
    return emit_creation(c, TYPED_TABLE_CREATE, table, key, capacity, &store);
}

/* ---- dispatch ---- */

static const struct {
    const char *name;
    bool (*check)(ReceiverCall *c);
} collection_methods[] = {
    {"len", check_len},
    {"has", check_has_or_remove},
    {"remove", check_has_or_remove},
    {"slice", check_slice},
    {"get", check_get},
    {"pop", check_pop},
    {"list", check_list_creation},
    {"table", check_table_creation},
};

bool check_collection_method(CheckContext *ctx, const AstNode *node,
                             const char *method, size_t ast_index,
                             bool *handled)
{
    ReceiverCall c = {0};
    size_t i;

    c.ctx = ctx;
    c.node = node;
    c.method = method;
    c.ast_index = ast_index;
    for (i = 0; i < NYX_COUNT_OF(collection_methods); i++) {
        if (strcmp(method, collection_methods[i].name) == 0) {
            *handled = true;
            return collection_methods[i].check(&c);
        }
    }
    return true;
}
