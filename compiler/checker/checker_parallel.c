#include "checker_internal.h"

/* The parallel block. Its arms run on other threads, so an arm can receive only
   values that carry no pointer into memory someone else could change: a
   sendable value is an inline value, copied into the arm, whose type holds no
   Text, Handle, List, Slice, Table, TextBuilder, Store, or function. The one
   shared thing is the block's own pool, which arms may allocate in. */

/* cores() is the number of processors the program may use, as an Int32. */
bool check_cores_call(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    TypedNode typed;
    size_t int32_type;

    if (node->children_count != 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CORES_ARGUMENTS);
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_INT32, &int32_type)) {
        return false;
    }
    typed = new_node(TYPED_CORES, int32_type, node->pos, TYPED_NO_NODE,
                     TYPED_NO_NODE);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

typedef struct {
    size_t arm;       /* TYPED_PARALLEL_ARM node */
    size_t type;      /* the result type */
    char name[NYX_IDENTIFIER_MAX];
    SourcePos pos;
} PendingVariable;

static const TypedNode *call_inside(const TypedProgram *program, size_t typed)
{
    const TypedNode *node = typed_program_node(program, typed);

    while (node != NULL && node->kind == TYPED_MATCH_BIND_EXPRESSION) {
        node = typed_program_node(program, node->right_child);
    }
    return node != NULL && node->kind == TYPED_FUNCTION_CALL ? node : NULL;
}

/* The value an argument holds. The checker orders arguments through
   temporaries, so an argument may be a reference to one that an enclosing
   binding fills. */
static const TypedNode *argument_value(const TypedProgram *program,
                                       size_t typed_call,
                                       const TypedNode *argument)
{
    const TypedNode *wrapper = typed_program_node(program, typed_call);

    if (argument == NULL || argument->kind != TYPED_MATCH_TEMPORARY) {
        return argument;
    }
    while (wrapper != NULL && wrapper->kind == TYPED_MATCH_BIND_EXPRESSION) {
        if (wrapper->integer_value_u == argument->integer_value_u) {
            return typed_program_node(program, wrapper->left_child);
        }
        wrapper = typed_program_node(program, wrapper->right_child);
    }
    return argument;
}

/* Whether an argument may be passed to an arm: a plain value, a channel, or
   the block's own pool (which is the only Store an arm may be given). */
static bool argument_allowed(CheckContext *ctx, const TypedNode *argument)
{
    const TypedType *type =
        argument == NULL ? NULL
                         : typed_program_type(ctx->program, argument->type_index);

    if (type != NULL && type->kind == NYX_TYPE_CHANNEL) {
        return true; /* names a channel in a pool; every use is synchronised */
    }
    return argument != NULL &&
           type_is_sendable(ctx->program, argument->type_index);
}

static bool is_pool_argument(CheckContext *ctx, size_t typed_call,
                             const TypedNode *argument, const char *pool_name)
{
    const TypedNode *value = argument_value(ctx->program, typed_call, argument);

    return pool_name != NULL && value != NULL &&
           value->kind == TYPED_IDENTIFIER && value->store_id == TYPED_NO_NODE &&
           strcmp(typed_program_symbol_text(ctx->program, value->name_index),
                  pool_name) == 0;
}

/* Every argument is a plain value, or the block's pool. */
static bool check_arm_arguments(CheckContext *ctx, size_t typed_call,
                                const TypedNode *call, const char *pool_name,
                                SourcePos pos)
{
    size_t i;

    for (i = 0; i < call->children_count; i++) {
        const TypedNode *argument = typed_program_node(
            ctx->program, *(const size_t *)list_get(&ctx->program->children,
                                                    call->children_start + i));
        const TypedType *type =
            argument == NULL
                ? NULL
                : typed_program_type(ctx->program, argument->type_index);
        bool is_store = type != NULL && type->kind == NYX_TYPE_STORE;

        if (is_store ? !is_pool_argument(ctx, typed_call, argument, pool_name)
                     : !argument_allowed(ctx, argument)) {
            error_report_at(ctx->errors, pos,
                            is_store ? MESSAGE_CHECKER_PARALLEL_STORE
                                     : MESSAGE_CHECKER_PARALLEL_ARGUMENT,
                            i + 1);
            return false;
        }
    }
    return true;
}

static bool check_arm_bounds(CheckContext *ctx, const AstNode *arm,
                             size_t int32_type, size_t *lo, size_t *hi)
{
    const AstNode *range = ast_get_node(ctx->ast, arm->right_child);
    const char *callee = range == NULL || range->kind != AST_CALL
                             ? NULL : ast_symbol_text(ctx->ast, range->name_index);

    if (callee == NULL || strcmp(callee, "range") != 0 ||
        range->children_count != 2) {
        error_report_at(ctx->errors, arm->pos,
                        MESSAGE_CHECKER_PARALLEL_LANE_RANGE);
        return false;
    }
    if (!check_expression_coerced(
            ctx, *(const size_t *)list_get(&ctx->ast->children,
                                           range->children_start),
            int32_type, lo) ||
        !check_expression_coerced(
            ctx, *(const size_t *)list_get(&ctx->ast->children,
                                           range->children_start + 1),
            int32_type, hi)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, arm->pos,
                            MESSAGE_CHECKER_PARALLEL_LANE_RANGE);
        }
        return false;
    }
    return true;
}

/* A repeated arm fills an existing Array, one element per lane. */
static bool check_lane_target(CheckContext *ctx, const AstNode *arm,
                              const VariableEntry *target, const char *name,
                              size_t result_type, size_t *capacity)
{
    const TypedType *array = typed_program_type(ctx->program, target->type_index);

    if (array == NULL || array->kind != NYX_TYPE_FIXED_LIST ||
        array->child_count != 1 ||
        !same_type(ctx->program,
                   *(const size_t *)list_get(&ctx->program->type_children,
                                             array->first_child),
                   result_type)) {
        error_report_at(ctx->errors, arm->pos,
                        MESSAGE_CHECKER_PARALLEL_LANE_TARGET, name);
        return false;
    }
    *capacity = array->fixed_length;
    return true;
}

/* A `with name = call` arm assigns a variable that already exists. */
static bool check_assigned_target(CheckContext *ctx, const AstNode *arm,
                                  const char *name, size_t result_type,
                                  size_t *capacity)
{
    const VariableEntry *target = lookup_variable(ctx, name);
    bool collecting = arm->right_child != AST_NO_NODE;

    if (target == NULL || target->is_const) {
        error_report_at(ctx->errors, arm->pos, MESSAGE_CHECKER_PARALLEL_TARGET,
                        name);
        return false;
    }
    if (collecting ? !check_lane_target(ctx, arm, target, name, result_type,
                                        capacity)
                   : !same_type(ctx->program, target->type_index, result_type)) {
        if (!collecting) {
            error_report_at(ctx->errors, arm->pos,
                            MESSAGE_CHECKER_PARALLEL_TARGET_TYPE, name);
        }
        return false;
    }
    if (!type_is_sendable(ctx->program, result_type)) {
        error_report_at(ctx->errors, arm->pos,
                        MESSAGE_CHECKER_PARALLEL_RESULT_PLAIN);
        return false;
    }
    invalidate_narrowing(ctx, name);
    return true;
}

typedef struct {
    const char *pool_name;
    size_t int32_type;
    List *pending;       /* PendingVariable */
    List *assigned;      /* char[NYX_IDENTIFIER_MAX] names assigned so far */
    List *arms;          /* typed arm indices */
} ParallelState;

/* An arm being checked. */
typedef struct {
    CheckContext *ctx;
    const AstNode *node;      /* the parallel block */
    const AstNode *arm;
    ParallelState *state;
    const char *callee;
    const char *binding;
    bool replicated;
    size_t lo;
    size_t hi;
    size_t typed_call;
    size_t result_type;
    size_t capacity;          /* of the Array a repeated arm fills */
} Arm;

/* The arm calls a plain function, other than the one it is in. */
static bool check_arm_callee(const Arm *a)
{
    CheckContext *ctx = a->ctx;
    const AstNode *call = ast_get_node(ctx->ast, a->arm->left_child);
    const FunctionEntry *function =
        a->callee == NULL ? NULL : find_function(ctx, a->callee);

    if (function == NULL || function->generic || function->is_method ||
        function->is_static || call->is_static) {
        error_report_at(ctx->errors, a->arm->pos, MESSAGE_CHECKER_PARALLEL_CALL);
        return false;
    }
    if (ctx->function_name != NULL && strcmp(a->callee, ctx->function_name) == 0) {
        error_report_at(ctx->errors, a->arm->pos, MESSAGE_CHECKER_PARALLEL_SELF);
        return false;
    }
    return true;
}

/* A repeated arm runs once per lane of a range and cannot bind a single
   result. */
static bool check_arm_lanes(Arm *a)
{
    CheckContext *ctx = a->ctx;

    if (!a->replicated) {
        return true;
    }
    if (a->arm->integer_value == 2) {
        error_report_at(ctx->errors, a->arm->pos,
                        MESSAGE_CHECKER_PARALLEL_LANE_RESULT);
        return false;
    }
    return check_arm_bounds(ctx, a->arm, a->state->int32_type, &a->lo, &a->hi) &&
           check_for_binding(ctx,
                             ast_symbol_text(ctx->ast, a->arm->second_name_index),
                             a->state->int32_type, a->arm->pos);
}

/* An arm with no result binding is a call statement, so a function that
   returns nothing is fine; binding its result is still an error. */
static bool check_arm_call(Arm *a)
{
    CheckContext *ctx = a->ctx;
    size_t lane_start = ctx->locals->length - (a->replicated ? 1 : 0);
    bool previous = ctx->in_expression_statement;
    size_t previous_call = ctx->statement_call_ast;
    const TypedNode *inner;
    bool checked;

    if (a->arm->integer_value == 0) {
        ctx->in_expression_statement = true;
        ctx->statement_call_ast = a->arm->left_child;
    }
    checked = check_expression(ctx, a->arm->left_child, &a->typed_call);
    ctx->in_expression_statement = previous;
    ctx->statement_call_ast = previous_call;
    ctx->locals->length = lane_start;
    inner = checked ? call_inside(ctx->program, a->typed_call) : NULL;
    if (checked && inner == NULL) {
        error_report_at(ctx->errors, a->arm->pos, MESSAGE_CHECKER_PARALLEL_CALL);
    }
    if (inner == NULL) {
        return false;
    }
    a->result_type = inner->type_index;
    return check_arm_arguments(ctx, a->typed_call, inner, a->state->pool_name,
                               a->arm->pos);
}

static bool already_assigned(const List *assigned, const char *name)
{
    size_t i;

    for (i = 0; i < assigned->length; i++) {
        if (strcmp((const char *)list_get(assigned, i), name) == 0) {
            return true;
        }
    }
    return false;
}

/* `with name = call`: the target is new to this block, exists, and takes the
   result. */
static bool check_arm_target(Arm *a)
{
    CheckContext *ctx = a->ctx;
    char name[NYX_IDENTIFIER_MAX] = {0};

    if (already_assigned(a->state->assigned, a->binding)) {
        error_report_at(ctx->errors, a->arm->pos,
                        MESSAGE_CHECKER_PARALLEL_DUPLICATE_TARGET, a->binding);
        return false;
    }
    if (!check_assigned_target(ctx, a->arm, a->binding, a->result_type,
                               &a->capacity)) {
        return false;
    }
    memcpy(name, a->binding, strlen(a->binding) + 1);
    return checker_ok(ctx->errors, list_push(a->state->assigned, name),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* `with var name = call`: the result becomes a local of the then section. */
static bool check_arm_new_variable(const Arm *a)
{
    CheckContext *ctx = a->ctx;

    if (a->node->right_child == AST_NO_NODE) {
        error_report_at(ctx->errors, a->arm->pos,
                        MESSAGE_CHECKER_PARALLEL_VAR_THEN);
        return false;
    }
    if (a->state->pool_name == NULL &&
        !type_is_sendable(ctx->program, a->result_type)) {
        error_report_at(ctx->errors, a->arm->pos,
                        MESSAGE_CHECKER_PARALLEL_RESULT_STORE);
        return false;
    }
    return true;
}

/* Arms are plain calls (0), assign a variable (1), or declare one (2). */
static bool check_arm_binding(Arm *a)
{
    if (a->arm->integer_value == 1) {
        return check_arm_target(a);
    }
    return a->arm->integer_value != 2 || check_arm_new_variable(a);
}

/* The typed arm, its lane range and its place in the block. */
static bool emit_arm(Arm *a)
{
    CheckContext *ctx = a->ctx;
    TypedNode typed = new_node(TYPED_PARALLEL_ARM, a->result_type, a->arm->pos,
                               a->typed_call, TYPED_NO_NODE);
    PendingVariable pending = {0};
    size_t index;

    typed.integer_value = a->arm->integer_value;
    typed.integer_value_u = a->capacity;
    typed.second_name_index = TYPED_NO_NODE;
    if (a->binding != NULL) {
        typed.name_index = checker_name(ctx, a->binding);
    }
    if (a->replicated) {
        typed.second_name_index = checker_name(
            ctx, ast_symbol_text(ctx->ast, a->arm->second_name_index));
        typed.children_start = ctx->program->children.length;
        typed.children_count = 2;
        checker_ok(ctx->errors, typed_program_push_child(ctx->program, a->lo),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        checker_ok(ctx->errors, typed_program_push_child(ctx->program, a->hi),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (!checker_push_node(ctx, &typed, &index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    checker_ok(ctx->errors, list_push(a->state->arms, &index),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    if (a->arm->integer_value == 2) {
        pending.arm = index;
        pending.type = a->result_type;
        pending.pos = a->arm->pos;
        memcpy(pending.name, a->binding, strlen(a->binding) + 1);
        checker_ok(ctx->errors, list_push(a->state->pending, &pending),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

static bool check_one_arm(CheckContext *ctx, const AstNode *node,
                          ParallelState *state, size_t arm_ast)
{
    const AstNode *arm = ast_get_node(ctx->ast, arm_ast);
    const AstNode *call = ast_get_node(ctx->ast, arm->left_child);
    Arm a = {.ctx = ctx,
             .node = node,
             .arm = arm,
             .state = state,
             .callee = call == NULL || call->kind != AST_CALL
                           ? NULL
                           : ast_symbol_text(ctx->ast, call->name_index),
             .binding = arm->name_index == AST_NO_NODE
                            ? NULL
                            : ast_symbol_text(ctx->ast, arm->name_index),
             .replicated = arm->right_child != AST_NO_NODE,
             .lo = TYPED_NO_NODE,
             .hi = TYPED_NO_NODE};

    return check_arm_callee(&a) && check_arm_lanes(&a) && check_arm_call(&a) &&
           check_arm_binding(&a) && emit_arm(&a);
}

/* `var name = result` for one result of the block. */
static bool declare_result(CheckContext *ctx, const PendingVariable *variable)
{
    TypedNode result = new_node(TYPED_PARALLEL_RESULT, variable->type,
                                variable->pos, TYPED_NO_NODE, TYPED_NO_NODE);
    TypedNode declaration;
    size_t result_index;
    size_t declaration_index;

    result.integer_value_u = variable->arm;
    if (!checker_push_node(ctx, &result, &result_index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    declaration = new_node(TYPED_LOCAL_DECLARATION, variable->type, variable->pos,
                           result_index, TYPED_NO_NODE);
    declaration.name_index = checker_name(ctx, variable->name);
    if (!checker_push_node(ctx, &declaration, &declaration_index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    return checker_ok(ctx->errors,
                      typed_program_push_child(ctx->program, declaration_index),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* The then block, with a declaration for each result in front of its
   statements. */
static bool merge_then_block(CheckContext *ctx, const AstNode *node,
                             const List *pending, size_t body, size_t *out)
{
    /* Adding nodes below can move the node list, so copy the block's fields
       now instead of reading them through a pointer afterwards. */
    const TypedNode *checked = typed_program_node(ctx->program, body);
    size_t body_start = checked == NULL ? 0 : checked->children_start;
    size_t body_count = checked == NULL ? 0 : checked->children_count;
    SourcePos body_pos = checked == NULL ? node->pos : checked->pos;
    size_t children_start = ctx->program->children.length;
    size_t i;

    for (i = 0; i < pending->length; i++) {
        if (!declare_result(ctx, list_get(pending, i))) {
            return false;
        }
    }
    for (i = 0; i < body_count; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       ctx->program,
                       *(const size_t *)list_get(&ctx->program->children,
                                                 body_start + i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return push_block(ctx, body_pos, children_start, pending->length + body_count,
                      out);
}

/* The then section starts with one declaration for every `with var`, so the
   results are ordinary locals there. */
static bool check_then_section(CheckContext *ctx, const AstNode *node,
                               const List *pending, size_t return_type,
                               size_t *out)
{
    size_t locals_start = ctx->locals->length;
    size_t body;
    size_t i;
    bool ok = true;

    for (i = 0; ok && i < pending->length; i++) {
        const PendingVariable *variable = list_get(pending, i);

        ok = check_for_binding(ctx, variable->name, variable->type,
                               variable->pos);
    }
    ok = ok && check_block(ctx, node->right_child, return_type, &body);
    if (ok && pending->length == 0) {
        *out = body;
    } else if (ok) {
        ok = merge_then_block(ctx, node, pending, body, out);
    }
    ctx->locals->length = locals_start;
    return ok;
}

/* The declarations before the first arm, as one block whose names stay visible
   to the arms and the then section. */
static bool check_setup(CheckContext *ctx, const AstNode *node,
                        size_t return_type, size_t *out)
{
    List declarations;
    size_t start;
    size_t i;
    bool ok = true;

    *out = TYPED_NO_NODE;
    if (node->integer_value == 0) {
        return true;
    }
    list_init(&declarations, sizeof(size_t));
    for (i = 0; ok && i < (size_t)node->integer_value; i++) {
        size_t typed;

        ok = check_statement(ctx,
                             *(const size_t *)list_get(&ctx->ast->children,
                                                       node->children_start + i),
                             return_type, &typed);
        if (ok) {
            checker_ok(ctx->errors, list_push(&declarations, &typed),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    start = push_typed_children(ctx, &declarations);
    ok = ok && push_block(ctx, node->pos, start, declarations.length, out);
    list_free(&declarations);
    return ok;
}

/* `parallel pool(capacity)`: the block's pool, a Store its arms may allocate
   in, visible as a constant local. */
static bool declare_pool(CheckContext *ctx, const AstNode *node,
                         const char *pool_name, size_t int32_type,
                         size_t *capacity)
{
    VariableEntry entry = {0};
    size_t store_type;

    if (name_is_taken(ctx, pool_name, true)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        pool_name);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, pool_name) ||
        !check_negative_capacity(ctx, node->left_child)) {
        return false;
    }
    if (!check_expression_coerced(ctx, node->left_child, int32_type, capacity)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_PARALLEL_CAPACITY);
        }
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_STORE, &store_type)) {
        return false;
    }
    memcpy(entry.name, pool_name, strlen(pool_name) + 1);
    entry.type_index = store_type;
    entry.is_const = true;
    checker_ok(ctx->errors, list_push(ctx->locals, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

static bool check_arms(CheckContext *ctx, const AstNode *node,
                       ParallelState *state)
{
    size_t i;

    for (i = (size_t)node->integer_value; i < node->children_count; i++) {
        if (!check_one_arm(ctx, node, state,
                           *(const size_t *)list_get(&ctx->ast->children,
                                                     node->children_start + i))) {
            return false;
        }
    }
    return true;
}

/* The pool, the setup, the arms and the then section, in order. */
static bool check_parallel_parts(CheckContext *ctx, const AstNode *node,
                                 ParallelState *state, size_t return_type,
                                 size_t *capacity, size_t *setup_block,
                                 size_t *then_block)
{
    return builtin_type(ctx, NYX_TYPE_INT32, &state->int32_type) &&
           (state->pool_name == NULL ||
            declare_pool(ctx, node, state->pool_name, state->int32_type,
                         capacity)) &&
           check_setup(ctx, node, return_type, setup_block) &&
           check_arms(ctx, node, state) &&
           (node->right_child == AST_NO_NODE ||
            check_then_section(ctx, node, state->pending, return_type,
                               then_block));
}

bool check_parallel(CheckContext *ctx, const AstNode *node, size_t ast_index,
                    size_t return_type, size_t *out_typed)
{
    const char *pool_name = node->name_index == AST_NO_NODE
                                ? NULL
                                : ast_symbol_text(ctx->ast, node->name_index);
    size_t locals_start = ctx->locals->length;
    size_t capacity = TYPED_NO_NODE;
    size_t then_block = TYPED_NO_NODE;
    size_t setup_block = TYPED_NO_NODE;
    List pending;
    List assigned;
    List arms;
    ParallelState state;
    TypedNode typed;
    bool ok;

    list_init(&pending, sizeof(PendingVariable));
    list_init(&assigned, NYX_IDENTIFIER_MAX);
    list_init(&arms, sizeof(size_t));
    state = (ParallelState){pool_name, 0, &pending, &assigned, &arms};
    ok = check_parallel_parts(ctx, node, &state, return_type, &capacity,
                              &setup_block, &then_block);
    typed = new_node(TYPED_PARALLEL, TYPED_NO_NODE, node->pos, capacity,
                     then_block);
    typed.integer_value = pool_name == NULL ? 0 : 1;
    typed.setup_block = setup_block;
    if (ok && pool_name != NULL) {
        typed.name_index = checker_name(ctx, pool_name);
    }
    typed.children_start = ok ? push_typed_children(ctx, &arms) : 0;
    typed.children_count = arms.length;
    ok = ok && emit_node(ctx, &typed, ast_index, out_typed);
    ctx->locals->length = locals_start;
    list_free(&pending);
    list_free(&assigned);
    list_free(&arms);
    return ok;
}
