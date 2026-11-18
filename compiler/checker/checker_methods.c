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
        if (type != NULL && type->kind == NYX_TYPE_STORE) {
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
    node->name_index = checker_name(ctx, store->name);
    return true;
}



/* name.load(), name.store(value), name.add(n) on a global Atomic. add returns
   the value the cell held before, so it can hand out unique tickets. */
static const struct {
    const char *name;
    TypedNodeKind kind;
    bool takes_value;
} atomic_operations[] = {
    {"load", TYPED_ATOMIC_LOAD, false},
    {"store", TYPED_ATOMIC_STORE, true},
    {"add", TYPED_ATOMIC_ADD, true},
};

/* The value an Atomic store or add takes, coerced to the cell's type. */
static bool atomic_operand(CheckContext *ctx, const AstNode *node,
                           const char *method, const AtomicEntry *entry,
                           size_t *coerced)
{
    size_t argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                    node->children_start);
    size_t value;

    if (ast_get_node(ctx->ast, argument_ast)->kind == AST_NAMED_ARG ||
        !check_expression(ctx, argument_ast, &value) ||
        !inject_into(ctx, value, entry->type_index, coerced) ||
        !check_default_numeric(ctx, *coerced)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_ATOMIC_ARGUMENTS, method,
                            MESSAGE_FRAGMENT_ATOMIC_VALUE);
        }
        return false;
    }
    return true;
}

/* The operation, with the right number of arguments, on a cell that can do
   it: add needs an integer. */
static bool check_atomic_shape(CheckContext *ctx, const AstNode *node,
                               const char *method, size_t operation,
                               const AtomicEntry *entry)
{
    bool takes_value = atomic_operations[operation].takes_value;
    const TypedType *element =
        typed_program_type(ctx->program, entry->type_index);

    if (node->right_child != AST_NO_NODE ||
        node->children_count != (takes_value ? 1u : 0u)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ATOMIC_ARGUMENTS,
                        method,
                        takes_value ? MESSAGE_FRAGMENT_ATOMIC_VALUE
                                    : MESSAGE_FRAGMENT_NO_ARGUMENTS);
        return false;
    }
    if (atomic_operations[operation].kind == TYPED_ATOMIC_ADD &&
        element->kind == NYX_TYPE_BOOL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ATOMIC_ADD_TYPE);
        return false;
    }
    return true;
}

bool check_atomic_method(CheckContext *ctx, const AstNode *node,
                         const char *method, size_t ast_index)
{
    const AstNode *receiver = ast_get_node(ctx->ast, node->left_child);
    const AtomicEntry *entry = find_atomic(
        ctx->atomics, ast_symbol_text(ctx->ast, receiver->name_index));
    size_t operation = 0;
    size_t type_index = entry->type_index;
    size_t coerced = TYPED_NO_NODE;
    TypedNode typed;

    while (operation < NYX_COUNT_OF(atomic_operations) &&
           strcmp(method, atomic_operations[operation].name) != 0) {
        operation++;
    }
    if (operation == NYX_COUNT_OF(atomic_operations)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ATOMIC_METHOD,
                        method);
        return false;
    }
    if (!check_atomic_shape(ctx, node, method, operation, entry) ||
        (atomic_operations[operation].takes_value &&
         !atomic_operand(ctx, node, method, entry, &coerced)) ||
        (atomic_operations[operation].kind == TYPED_ATOMIC_STORE &&
         !builtin_type(ctx, NYX_TYPE_NIL, &type_index))) {
        return false;
    }
    typed = new_node(atomic_operations[operation].kind, type_index, node->pos,
                     coerced, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, entry->name);
    typed.store_id = entry->id;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* A method call is decided by the first rule that recognises it. */
typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    const char *method;
    size_t ast_index;
} MethodCall;

typedef enum {
    METHOD_NEXT,
    METHOD_REJECTED,
    METHOD_ACCEPTED
} MethodStep;

typedef MethodStep (*MethodRule)(const MethodCall *call);

static MethodStep method_step(bool ok)
{
    return ok ? METHOD_ACCEPTED : METHOD_REJECTED;
}

static MethodStep method_needs_name(const MethodCall *call)
{
    if (call->method != NULL) {
        return METHOD_NEXT;
    }
    error_report_at(call->ctx->errors, call->node->pos,
                    MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
    return METHOD_REJECTED;
}

/* The name of the variable a call is made on, when the receiver is a bare
   name. */
static const char *receiver_name(const MethodCall *call)
{
    const AstNode *receiver =
        ast_get_node(call->ctx->ast, call->node->left_child);

    return receiver != NULL && receiver->kind == AST_IDENTIFIER
               ? ast_symbol_text(call->ctx->ast, receiver->name_index)
               : NULL;
}

/* A global Atomic or Channel named as the receiver, unless a local hides it. */
static MethodStep method_atomic(const MethodCall *call)
{
    const char *name = receiver_name(call);

    if (name == NULL || find_variable(call->ctx->locals, name) != NULL ||
        find_atomic(call->ctx->atomics, name) == NULL) {
        return METHOD_NEXT;
    }
    return method_step(check_atomic_method(call->ctx, call->node, call->method,
                                           call->ast_index));
}

static MethodStep method_channel(const MethodCall *call)
{
    const char *name = receiver_name(call);
    const VariableEntry *local =
        name == NULL ? NULL : find_variable(call->ctx->locals, name);
    bool global = name != NULL && local == NULL &&
                  find_channel(call->ctx->channels, name) != NULL;
    bool held = local != NULL &&
                type_is_channel(call->ctx->program, local->type_index);

    if (!global && !held) {
        return METHOD_NEXT;
    }
    return method_step(check_channel_method(call->ctx, call->node, call->method,
                                            call->ast_index));
}

/* The type of the receiver: a variable's type as narrowed here, or the type of
   the checked receiver expression. Returns false when the receiver is wrong;
   `decided` is set when that has already settled the whole call. */
/* The type of a receiver, or the reason the call is already decided: a receiver
   that fails to check has said why, and a global used inside a function is
   reported by checking it. */
typedef struct {
    size_t type_index;
    bool decided;
    bool ok;
} ReceiverType;

/* A receiver that is not a bare name: check it and take its type. */
static ReceiverType receiver_of_expression(const MethodCall *call)
{
    CheckContext *ctx = call->ctx;
    size_t checked;
    bool ok = check_expression(ctx, call->node->left_child, &checked);
    const TypedNode *typed = ok ? typed_program_node(ctx->program, checked) : NULL;

    return (ReceiverType){typed == NULL ? TYPED_NO_NODE : typed->type_index, !ok,
                          ok};
}

/* A variable's type as narrowed here. */
static size_t narrowed_type(const CheckContext *ctx, const char *name,
                            const VariableEntry *variable)
{
    const TypedType *declared =
        typed_program_type(ctx->program, variable->type_index);
    const Narrowing *narrowing =
        declared != NULL && declared->kind == NYX_TYPE_UNION
            ? prior_narrowing(ctx, name, false, 0)
            : NULL;

    return narrowing == NULL ? variable->type_index : narrowing->type_index;
}

/* A receiver that is a bare name. A name that is not a visible variable has no
   type here; the built-in families look at it themselves. */
static ReceiverType receiver_of_name(const MethodCall *call, const char *name)
{
    CheckContext *ctx = call->ctx;
    const VariableEntry *variable = name == NULL ? NULL : lookup_variable(ctx, name);

    if (variable == NULL && ctx->in_function && name != NULL &&
        find_variable(ctx->globals, name) != NULL) {
        bool ok = check_expression(ctx, call->node->left_child, &(size_t){0});

        return (ReceiverType){TYPED_NO_NODE, true, ok};
    }
    return (ReceiverType){
        variable == NULL ? TYPED_NO_NODE : narrowed_type(ctx, name, variable),
        false, true};
}

/* A method declared on a struct. */
/* The method of that name declared on the struct type, if the type is one. */
static const FunctionEntry *struct_method(const CheckContext *ctx,
                                          size_t type_index, const char *method)
{
    const TypedType *type = typed_program_type(ctx->program, type_index);
    const FunctionEntry *function;

    if (type == NULL || type->kind != NYX_TYPE_STRUCT) {
        return NULL;
    }
    function = find_associated_function(
        ctx->functions,
        typed_program_symbol_text(ctx->program, type->source_name_index), method);
    return function != NULL && function->is_method ? function : NULL;
}

/* A method declared on a struct. */
static MethodStep method_struct(const MethodCall *call)
{
    CheckContext *ctx = call->ctx;
    const AstNode *receiver = ast_get_node(ctx->ast, call->node->left_child);
    ReceiverType found = receiver != NULL && receiver->kind == AST_IDENTIFIER
                             ? receiver_of_name(call, receiver_name(call))
                             : receiver_of_expression(call);
    const FunctionEntry *function;

    if (found.decided) {
        return method_step(found.ok);
    }
    function = struct_method(ctx, found.type_index, call->method);
    if (function == NULL) {
        return METHOD_NEXT;
    }
    if (call->node->right_child != AST_NO_NODE) {
        error_report_at(ctx->errors, call->node->pos,
                        MESSAGE_CHECKER_MEMBER_TYPE_ARG, call->method);
        return METHOD_REJECTED;
    }
    return method_step(
        function->generic
            ? check_generic_call(ctx, call->node,
                                 find_function_index(ctx, function->name),
                                 call->ast_index)
            : check_associated_function_call(ctx, call->node, function,
                                             call->ast_index, true));
}

/* Methods of the built-in types, each family deciding for itself whether the
   call is one of its own. */
typedef bool (*MethodFamily)(CheckContext *ctx, const AstNode *node,
                             const char *method, size_t ast_index,
                             bool *handled);

static const MethodFamily method_families[] = {
    check_collection_method, check_string_method, check_error_method,
    check_store_method,
};

static MethodStep method_builtin(const MethodCall *call)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(method_families); i++) {
        bool handled = false;

        if (!method_families[i](call->ctx, call->node, call->method,
                                call->ast_index, &handled)) {
            return METHOD_REJECTED;
        }
        if (handled) {
            return METHOD_ACCEPTED;
        }
    }
    return METHOD_NEXT;
}

static MethodStep method_unknown(const MethodCall *call)
{
    error_report_at(call->ctx->errors, call->node->pos,
                    call->node->right_child != AST_NO_NODE
                        ? MESSAGE_CHECKER_MEMBER_TYPE_ARG
                        : MESSAGE_CHECKER_UNKNOWN_MEMBER,
                    call->method);
    return METHOD_REJECTED;
}

static const MethodRule method_rules[] = {
    method_needs_name, method_atomic, method_channel, method_struct,
    method_builtin, method_unknown,
};

bool check_member_call(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    MethodCall call = {ctx, node, ast_symbol_text(ctx->ast, node->name_index),
                       ast_index};
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(method_rules); i++) {
        MethodStep step = method_rules[i](&call);

        if (step != METHOD_NEXT) {
            return step == METHOD_ACCEPTED;
        }
    }
    return false;
}

/* ---- the start of a call on a value: shared by the methods of the built-in
   types ---- */

bool receiver_reject(const ReceiverCall *c, const char *message)
{
    error_report_at(c->ctx->errors, c->node->pos, message, c->method);
    return false;
}

/* Check the receiver and read its type. */
bool receiver_check(ReceiverCall *c)
{
    const TypedNode *node;
    const TypedType *type;

    if (!check_expression(c->ctx, c->node->left_child, &c->receiver)) {
        return false;
    }
    node = typed_program_node(c->ctx->program, c->receiver);
    type = node == NULL ? NULL
                        : typed_program_type(c->ctx->program, node->type_index);
    c->receiver_name = node == NULL ? TYPED_NO_NODE : node->name_index;
    c->type_index = node == NULL ? TYPED_NO_NODE : node->type_index;
    c->kind = type == NULL ? NYX_TYPE_UNKNOWN : type->kind;
    c->child_count = type == NULL ? 0 : type->child_count;
    c->first_child = type == NULL ? 0 : type->first_child;
    return true;
}

/* The common start of a call: no type arguments, `argc` arguments, then the
   receiver. */
bool receiver_prepare(ReceiverCall *c, size_t argc)
{
    if (c->node->right_child != AST_NO_NODE) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_TYPE_ARG);
    }
    if (c->node->children_count != argc) {
        return receiver_reject(c, MESSAGE_CHECKER_MEMBER_ARGS);
    }
    return receiver_check(c);
}

size_t receiver_argument(const ReceiverCall *c, size_t i)
{
    return *(const size_t *)list_get(&c->ctx->ast->children,
                                     c->node->children_start + i);
}

bool receiver_emit(const ReceiverCall *c, TypedNode *typed)
{
    typed->pos = c->node->pos;
    return emit_node(c->ctx, typed, c->ast_index, &(size_t){0});
}

/* The receiver is a bare variable, which a call that changes it needs. */
bool receiver_is_variable(const ReceiverCall *c)
{
    const AstNode *receiver = ast_get_node(c->ctx->ast, c->node->left_child);

    return receiver != NULL && receiver->kind == AST_IDENTIFIER &&
           c->receiver_name != TYPED_NO_NODE;
}
