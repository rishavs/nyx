#include "checker_internal.h"

/* Global channels. A channel is a resource like a Store or an Atomic: it is
   declared once at program level, its name is visible in every function, and it
   is used only through its operations. What crosses it is a copy of a plain
   value, so two threads never share an object through a channel. */

typedef enum {
    RESULT_BOOL,
    RESULT_NIL,
    RESULT_OPTIONAL
} ChannelResult;

static const struct {
    const char *name;
    ChannelOp op;
    bool takes_value;
    ChannelResult result;
} channel_operations[] = {
    {"send", CHANNEL_OP_SEND, true, RESULT_BOOL},
    {"recv", CHANNEL_OP_RECV, false, RESULT_OPTIONAL},
    {"try_send", CHANNEL_OP_TRY_SEND, true, RESULT_BOOL},
    {"try_recv", CHANNEL_OP_TRY_RECV, false, RESULT_OPTIONAL},
    {"close", CHANNEL_OP_CLOSE, false, RESULT_NIL},
    {"closed", CHANNEL_OP_CLOSED, false, RESULT_BOOL},
};

/* The call `Channel(n)` or `Every(n)`: a queue of n slots, or a timer that
   ticks every n milliseconds. */
static bool channel_constructor(CheckContext *ctx, const AstNode *node,
                                const char *name, bool *timer, size_t *argument)
{
    const AstNode *initialiser = ast_get_node(ctx->ast, node->left_child);
    const char *constructor =
        initialiser == NULL || initialiser->kind != AST_CALL ||
                initialiser->children_count != 1
            ? NULL
            : ast_symbol_text(ctx->ast, initialiser->name_index);

    *timer = constructor != NULL && strcmp(constructor, "Every") == 0;
    if (constructor == NULL || (!*timer && strcmp(constructor, "Channel") != 0)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CHANNEL_INITIALIZER, name);
        return false;
    }
    *argument = *(const size_t *)list_get(&ctx->ast->children,
                                          initialiser->children_start);
    return true;
}

/* The initialiser's constant: a queue's capacity or a timer's interval. */
static bool channel_initialiser(CheckContext *ctx, const AstNode *node,
                                const char *name, bool *timer, int *amount)
{
    bool is_constant = false;
    size_t argument;

    if (!channel_constructor(ctx, node, name, timer, &argument)) {
        return false;
    }
    if (ast_get_node(ctx->ast, argument)->kind == AST_NAMED_ARG ||
        !const_int_eval(ctx, argument, amount, &is_constant) || !is_constant) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos,
                            *timer ? MESSAGE_CHECKER_CHANNEL_TIMER_INTERVAL
                                   : MESSAGE_CHECKER_CHANNEL_INITIALIZER,
                            name);
        }
        return false;
    }
    return true;
}

/* An element is a plain value that is neither Nil nor a union, small enough to
   copy by value. */
bool check_channel_element(CheckContext *ctx, SourcePos pos, size_t element)
{
    const TypedType *type = typed_program_type(ctx->program, element);

    if (type == NULL || type->kind == NYX_TYPE_NIL ||
        type->kind == NYX_TYPE_UNION) {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_CHANNEL_UNION);
        return false;
    }
    if (!type_is_sendable(ctx->program, element)) {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_CHANNEL_ELEMENT);
        return false;
    }
    return check_type_value_size(ctx, element, pos);
}

/* A global queue's whole buffer, being static, has a bounded size. */
static bool check_queue_shape(CheckContext *ctx, const AstNode *node,
                              size_t element, int capacity)
{
    size_t bytes;

    if (!check_channel_element(ctx, node->pos, element) ||
        !type_value_bytes(ctx, element, &bytes)) {
        return false;
    }
    if (capacity < 0 || capacity > NYX_MAX_CHANNEL_CAPACITY ||
        (size_t)capacity > NYX_MAX_CHANNEL_BYTES / (bytes == 0 ? 1 : bytes)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CHANNEL_CAPACITY,
                        NYX_MAX_CHANNEL_CAPACITY, NYX_MAX_CHANNEL_BYTES);
        return false;
    }
    return true;
}

/* A timer yields Int64 ticks at a positive interval. */
static bool check_timer_shape(CheckContext *ctx, const AstNode *node,
                              size_t element, int interval)
{
    const TypedType *type = typed_program_type(ctx->program, element);

    if (type == NULL || type->kind != NYX_TYPE_INT64) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CHANNEL_TIMER_TYPE);
        return false;
    }
    if (interval < 1) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CHANNEL_TIMER_INTERVAL);
        return false;
    }
    return true;
}

static bool register_channel(CheckContext *ctx, const AstNode *node,
                             const char *name, size_t element, int interval,
                             bool rendezvous, ChannelEntry *entry)
{
    if (name_is_taken(ctx, name, true)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, strlen(name) + 1);
    entry->id = ctx->channels->length;
    entry->element = element;
    entry->interval = interval;
    entry->rendezvous = rendezvous;
    if (!optional_type(ctx, element, &entry->optional)) {
        return false;
    }
    checker_ok(ctx->errors, list_push(ctx->channels, entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

/* `global name: Channel<T> = Channel(capacity)`, or a timer,
   `global name: Channel<Int64> = Every(milliseconds)`. */
/* The declaration is a global, not a const, and names one element type. */
static bool channel_declared_element(CheckContext *ctx, const AstNode *node,
                                     const char *name, size_t *element)
{
    const AstNode *annotation = ast_get_node(ctx->ast, node->right_child);

    if (!node->is_global || node->is_const) {
        error_report_at(ctx->errors, node->pos,
                        node->is_const ? MESSAGE_CHECKER_CONST_STORE
                                       : MESSAGE_CHECKER_CHANNEL_NOT_GLOBAL);
        return false;
    }
    if (annotation->children_count != 1) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CHANNEL_ELEMENT);
        return false;
    }
    return resolve_type_node(ctx,
                             *(const size_t *)list_get(&ctx->ast->children,
                                                       annotation->children_start),
                             name, element);
}

static bool emit_channel_declaration(CheckContext *ctx, const AstNode *node,
                                    const char *name, size_t ast_index,
                                    const ChannelEntry *entry, bool timer,
                                    int amount)
{
    TypedNode typed = new_node(TYPED_CHANNEL_DECLARATION, entry->element,
                               node->pos, TYPED_NO_NODE, TYPED_NO_NODE);

    typed.auxiliary_type = entry->optional;
    typed.name_index = checker_name(ctx, name);
    typed.store_id = entry->id;
    typed.integer_value = timer ? 1 : amount; /* 0 is a rendezvous channel */
    typed.integer_value_u = timer ? (uint64_t)amount : 0;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* `global name: Channel<T> = Channel(capacity)`, or a timer,
   `global name: Channel<Int64> = Every(milliseconds)`. */
bool check_channel_declaration(CheckContext *ctx, const AstNode *node,
                               const char *name, size_t ast_index)
{
    ChannelEntry entry;
    size_t element;
    int amount = 0;
    bool timer = false;

    if (!channel_declared_element(ctx, node, name, &element) ||
        !channel_initialiser(ctx, node, name, &timer, &amount) ||
        !(timer ? check_timer_shape(ctx, node, element, amount)
                : check_queue_shape(ctx, node, element, amount)) ||
        !register_channel(ctx, node, name, element, timer ? amount : 0,
                          !timer && amount == 0, &entry)) {
        return false;
    }
    return emit_channel_declaration(ctx, node, name, ast_index, &entry, timer,
                                    amount);
}

static const char *argument_description(bool takes_value)
{
    return takes_value ? MESSAGE_FRAGMENT_CHANNEL_VALUE
                       : MESSAGE_FRAGMENT_NO_ARGUMENTS;
}

/* What an operation acts on: a global channel by name, or a variable holding a
   Channel<T> from a pool. */
typedef struct {
    const ChannelEntry *global; /* NULL for a Channel value */
    size_t receiver;            /* the typed variable, for a Channel value */
    size_t element;
    size_t optional;
    bool rendezvous;
    char name[NYX_IDENTIFIER_MAX];
} ChannelTarget;

/* A global channel named as the receiver, unless a local hides it. */
static const ChannelEntry *named_global_channel(const CheckContext *ctx,
                                                const char *name)
{
    return find_variable(ctx->locals, name) == NULL
               ? find_channel(ctx->channels, name)
               : NULL;
}

/* A Channel<T> held in a variable, as a value. */
static bool channel_value_target(CheckContext *ctx, const AstNode *receiver,
                                 size_t receiver_ast, ChannelTarget *target)
{
    const TypedType *type;
    size_t typed;

    if (!check_expression(ctx, receiver_ast, &typed)) {
        return false;
    }
    type = typed_program_type(ctx->program,
                              typed_program_node(ctx->program, typed)->type_index);
    if (type == NULL || type->kind != NYX_TYPE_CHANNEL || type->child_count != 1) {
        error_report_at(ctx->errors, receiver->pos,
                        MESSAGE_CHECKER_CHANNEL_METHOD, target->name);
        return false;
    }
    target->receiver = typed;
    target->element = *(const size_t *)list_get(&ctx->program->type_children,
                                                type->first_child);
    return optional_type(ctx, target->element, &target->optional);
}

static bool resolve_channel_target(CheckContext *ctx, size_t receiver_ast,
                                   ChannelTarget *target)
{
    const AstNode *receiver = ast_get_node(ctx->ast, receiver_ast);
    const char *name = receiver == NULL || receiver->kind != AST_IDENTIFIER
                           ? NULL
                           : ast_symbol_text(ctx->ast, receiver->name_index);

    memset(target, 0, sizeof(*target));
    if (name == NULL) {
        error_report_at(ctx->errors,
                        receiver == NULL ? (SourcePos){0} : receiver->pos,
                        MESSAGE_CHECKER_WHEN_OPERATION);
        return false;
    }
    memcpy(target->name, name, strlen(name) + 1);
    target->global = named_global_channel(ctx, name);
    if (target->global == NULL) {
        return channel_value_target(ctx, receiver, receiver_ast, target);
    }
    target->element = target->global->element;
    target->optional = target->global->optional;
    target->rendezvous = target->global->rendezvous;
    return true;
}

/* Types `name.method(...)` for a channel and fills `typed`; the caller pushes
   it. A send's value is checked and coerced here and returned in `value`. */
static size_t channel_operation_for(const char *method)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(channel_operations); i++) {
        if (strcmp(channel_operations[i].name, method) == 0) {
            return i;
        }
    }
    return NYX_COUNT_OF(channel_operations);
}

/* The value a send carries, coerced to the channel's element type. */
static bool channel_send_value(CheckContext *ctx, const AstNode *node,
                               const char *method, size_t element, size_t *value)
{
    size_t argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                    node->children_start);
    size_t checked;

    if (ast_get_node(ctx->ast, argument_ast)->kind == AST_NAMED_ARG ||
        !check_expression(ctx, argument_ast, &checked) ||
        !inject_into(ctx, checked, element, value) ||
        !check_default_numeric(ctx, *value)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CHANNEL_ARGUMENTS, method,
                            argument_description(true));
        }
        return false;
    }
    return true;
}

/* The type an operation yields. */
static bool channel_result_type(CheckContext *ctx, const ChannelTarget *target,
                                ChannelResult result, size_t *type)
{
    if (result == RESULT_OPTIONAL) {
        *type = target->optional;
        return true;
    }
    return builtin_type(ctx, result == RESULT_NIL ? NYX_TYPE_NIL : NYX_TYPE_BOOL,
                        type);
}

/* Types `name.method(...)` for a channel and fills `typed`; the caller pushes
   it. A send's value is checked and coerced here and returned in `value`. */
/* The operation suits the channel and takes the right number of arguments. */
static bool check_channel_shape(CheckContext *ctx, const AstNode *node,
                                const char *method, const ChannelTarget *target,
                                size_t operation)
{
    bool takes_value = channel_operations[operation].takes_value;

    if (target->rendezvous &&
        channel_operations[operation].op == CHANNEL_OP_TRY_SEND) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CHANNEL_RENDEZVOUS_SEND);
        return false;
    }
    if (node->right_child != AST_NO_NODE ||
        node->children_count != (takes_value ? 1u : 0u)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CHANNEL_ARGUMENTS,
                        method, argument_description(takes_value));
        return false;
    }
    return true;
}

/* Types `name.method(...)` for a channel and fills `typed`; the caller pushes
   it. A send's value is checked and coerced here and returned in `value`. */
bool check_channel_operation(CheckContext *ctx, const AstNode *node,
                             const char *method, TypedNode *typed,
                             size_t *value)
{
    ChannelTarget target;
    size_t index = channel_operation_for(method);
    size_t result_type;

    if (!resolve_channel_target(ctx, node->left_child, &target)) {
        return false;
    }
    if (index == NYX_COUNT_OF(channel_operations)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CHANNEL_METHOD,
                        method);
        return false;
    }
    *value = TYPED_NO_NODE;
    if (!check_channel_shape(ctx, node, method, &target, index) ||
        (channel_operations[index].takes_value &&
         !channel_send_value(ctx, node, method, target.element, value)) ||
        !channel_result_type(ctx, &target, channel_operations[index].result,
                             &result_type)) {
        return false;
    }
    *typed = new_node(TYPED_CHANNEL_OP, result_type, node->pos, *value,
                      target.global != NULL ? TYPED_NO_NODE : target.receiver);
    typed->auxiliary_type = target.element;
    typed->store_id = target.global != NULL ? target.global->id : TYPED_NO_NODE;
    typed->integer_value = (int)channel_operations[index].op;
    typed->name_index = checker_name(ctx, target.name);
    return true;
}

bool check_channel_method(CheckContext *ctx, const AstNode *node,
                          const char *method, size_t ast_index)
{
    TypedNode typed;
    size_t value;

    if (!check_channel_operation(ctx, node, method, &typed, &value)) {
        return false;
    }
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* when: wait until one of several channel operations can proceed and run the
   arm of the first that can. Each arm is `with [name =] channel.recv() then` or
   `with [name =] channel.send(value) then`; the name holds what the operation
   returned, and is declared at the start of the arm's block. */
/* A `when` arm waits on a receive or a send. */
static const char *when_method(const CheckContext *ctx, const AstNode *call)
{
    const char *method = call == NULL || call->kind != AST_MEMBER_CALL
                             ? NULL
                             : ast_symbol_text(ctx->ast, call->name_index);

    return method != NULL &&
                   (strcmp(method, "recv") == 0 || strcmp(method, "send") == 0)
               ? method
               : NULL;
}

/* A send arm cannot wait for a receiver to take the value after the lock is
   released, so a global rendezvous channel has no send arm. */
static bool is_rendezvous_send(const CheckContext *ctx, const TypedNode *typed)
{
    return typed->integer_value == CHANNEL_OP_SEND &&
           typed->store_id != TYPED_NO_NODE &&
           ((const ChannelEntry *)list_get(ctx->channels, typed->store_id))
               ->rendezvous;
}

static bool when_operation(CheckContext *ctx, const AstNode *arm,
                           TypedNode *typed, size_t *value)
{
    const AstNode *call = ast_get_node(ctx->ast, arm->left_child);
    const char *method = when_method(ctx, call);

    if (method == NULL) {
        error_report_at(ctx->errors, arm->pos, MESSAGE_CHECKER_WHEN_OPERATION);
        return false;
    }
    if (!check_channel_operation(ctx, call, method, typed, value)) {
        return false;
    }
    if (is_rendezvous_send(ctx, typed)) {
        error_report_at(ctx->errors, arm->pos,
                        MESSAGE_CHECKER_CHANNEL_RENDEZVOUS_SEND);
        return false;
    }
    return true;
}

/* The arm's block, with a declaration of the result in front of it. */
/* The arm's block, with a declaration of the result in front of it. */
static bool when_arm_body(CheckContext *ctx, const AstNode *arm, size_t arm_index,
                          size_t result_type, size_t return_type, size_t *out)
{
    const char *name = ast_symbol_text(ctx->ast, arm->name_index);
    size_t locals_start = ctx->locals->length;
    TypedNode result = new_node(TYPED_WHEN_RESULT, result_type, arm->pos,
                                TYPED_NO_NODE, TYPED_NO_NODE);
    TypedNode declaration;
    size_t body;
    size_t body_start;
    size_t body_count;
    SourcePos body_pos;
    size_t result_index;
    size_t declaration_index;
    size_t children_start;
    size_t i;
    bool ok = check_for_binding(ctx, name, result_type, arm->pos) &&
              check_block(ctx, arm->right_child, return_type, &body);

    if (ok) {
        /* Adding nodes can move the node list, so copy the block's fields now. */
        const TypedNode *checked = typed_program_node(ctx->program, body);

        body_start = checked == NULL ? 0 : checked->children_start;
        body_count = checked == NULL ? 0 : checked->children_count;
        body_pos = checked == NULL ? arm->pos : checked->pos;
        result.integer_value_u = arm_index;
        ok = checker_push_node(ctx, &result, &result_index,
                               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (ok) {
        declaration = new_node(TYPED_LOCAL_DECLARATION, result_type, arm->pos,
                               result_index, TYPED_NO_NODE);
        declaration.name_index = checker_name(ctx, name);
        children_start = ctx->program->children.length;
        ok = checker_push_node(ctx, &declaration, &declaration_index,
                               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (ok) {
        checker_ok(ctx->errors,
                   typed_program_push_child(ctx->program, declaration_index),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        for (i = 0; i < body_count; i++) {
            checker_ok(ctx->errors,
                       typed_program_push_child(
                           ctx->program,
                           *(const size_t *)list_get(&ctx->program->children,
                                                     body_start + i)),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
        ok = push_block(ctx, body_pos, children_start, 1 + body_count, out);
    }
    ctx->locals->length = locals_start;
    return ok;
}

static bool check_when_arm(CheckContext *ctx, size_t arm_ast, size_t return_type,
                           size_t *out_arm)
{
    const AstNode *arm = ast_get_node(ctx->ast, arm_ast);
    TypedNode operation, typed = {0};
    size_t value, operation_index, body, result_type;

    if (!when_operation(ctx, arm, &operation, &value)) {
        return false;
    }
    result_type = operation.type_index;
    if (!checker_push_node(ctx, &operation, &operation_index, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    typed.kind = TYPED_WHEN_ARM;
    typed.type_index = result_type;
    typed.pos = arm->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = operation_index;
    typed.right_child = TYPED_NO_NODE;
    if (!checker_push_node(ctx, &typed, out_arm, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    if (arm->name_index == AST_NO_NODE) {
        if (!check_block(ctx, arm->right_child, return_type, &body)) {
            return false;
        }
    } else {
        const char *name = ast_symbol_text(ctx->ast, arm->name_index);
        size_t symbol = typed_program_copy_symbol(ctx->program, name,
                                                  strlen(name));

        if (symbol == TYPED_NO_NODE ||
            !when_arm_body(ctx, arm, *out_arm, result_type, return_type, &body)) {
            return false;
        }
        ((TypedNode *)list_get(&ctx->program->nodes, *out_arm))->name_index =
            symbol;
    }
    ((TypedNode *)list_get(&ctx->program->nodes, *out_arm))->right_child = body;
    return true;
}

bool check_when(CheckContext *ctx, const AstNode *node, size_t ast_index,
                size_t return_type, size_t *out_typed)
{
    TypedNode typed = {0};
    List arms;
    size_t else_block = TYPED_NO_NODE, i;
    bool ok = false;

    list_init(&arms, sizeof(size_t));
    for (i = 0; i < node->children_count; i++) {
        size_t arm;

        if (!check_when_arm(ctx,
                            *(const size_t *)list_get(&ctx->ast->children,
                                                      node->children_start + i),
                            return_type, &arm)) {
            goto done;
        }
        checker_ok(ctx->errors, list_push(&arms, &arm), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (node->right_child != AST_NO_NODE &&
        !check_block(ctx, node->right_child, return_type, &else_block)) {
        goto done;
    }
    typed.kind = TYPED_WHEN;
    typed.type_index = TYPED_NO_NODE;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = TYPED_NO_NODE;
    typed.right_child = else_block;
    typed.children_start = ctx->program->children.length;
    typed.children_count = arms.length;
    for (i = 0; i < arms.length; i++) {
        checker_ok(ctx->errors, typed_program_push_child(ctx->program, *(const size_t *)list_get(&arms, i)), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    ok = emit_node(ctx, &typed, ast_index, out_typed);

done:
    list_free(&arms);
    return ok;
}

/* pool.channel<T>(capacity): a channel in a parallel pool, as a value. The
   pool never moves, so a thread blocked on the channel never finds it gone;
   the generated program traps if the Store is not a pool. */
/* pool.channel<T>(capacity): a channel in a parallel pool, as a value. The
   pool never moves, so a thread blocked on the channel never finds it gone;
   the generated program traps if the Store is not a pool. */
/* The channel type for pool.channel<T>(...), and the optional type its
   receives yield. */
static bool channel_create_types(CheckContext *ctx, const AstNode *node,
                                 size_t *element, size_t *channel,
                                 size_t *optional)
{
    return resolve_type_in_env(ctx, node->right_child, ctx->env, "channel",
                               element) &&
           check_channel_element(ctx, node->pos, *element) &&
           channel_type(ctx, *element, channel) &&
           optional_type(ctx, *element, optional);
}

/* The capacity of pool.channel<T>(capacity), as an Int32. */
static bool channel_create_capacity(CheckContext *ctx, const AstNode *node,
                                    size_t *coerced)
{
    size_t capacity_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                    node->children_start);
    size_t capacity;
    size_t int32_type;

    if (!check_negative_capacity(ctx, capacity_ast)) {
        return false;
    }
    if (!check_expression(ctx, capacity_ast, &capacity) ||
        !builtin_type(ctx, NYX_TYPE_INT32, &int32_type) ||
        !inject_into(ctx, capacity, int32_type, coerced)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_CHANNEL_CREATE);
        }
        return false;
    }
    return true;
}

bool check_channel_create(CheckContext *ctx, const AstNode *node,
                          const char *method, size_t ast_index, bool *handled)
{
    StoreEntry store = {0};
    TypedNode typed;
    size_t element;
    size_t channel;
    size_t optional;
    size_t coerced;

    *handled = true;
    if (!member_store(ctx, node, method, &store)) {
        return false;
    }
    if (node->right_child == AST_NO_NODE || node->children_count != 1) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CHANNEL_CREATE);
        return false;
    }
    if (!channel_create_types(ctx, node, &element, &channel, &optional) ||
        !channel_create_capacity(ctx, node, &coerced)) {
        return false;
    }
    typed = new_node(TYPED_CHANNEL_CREATE, channel, node->pos, coerced,
                     TYPED_NO_NODE);
    typed.auxiliary_type = element;
    typed.integer_value_u = optional;
    set_store_reference(ctx, &typed, &store);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}
