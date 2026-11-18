#include "checker_internal.h"

/* Generic functions. A call infers the function's type parameters from its
   arguments, finds or creates the instantiation for those types, and calls
   it; the body of an instantiation is checked once, under the types it was
   instantiated with. */

FunctionInstantiation *find_function_instantiation(
    CheckContext *ctx, size_t entry_index, const size_t *arguments,
    size_t argument_count)
{
    size_t i;

    for (i = 0; i < ctx->function_instantiations->length; i++) {
        FunctionInstantiation *record =
            (FunctionInstantiation *)list_get(ctx->function_instantiations, i);
        size_t j;
        bool same = record->entry == entry_index &&
                    record->argument_count == argument_count;

        for (j = 0; same && j < argument_count; j++) {
            same = same_type(ctx->program, record->arguments[j], arguments[j]);
        }
        if (same) {
            return record;
        }
    }
    return NULL;
}

/* ---- the body of an instantiation ---- */

/* What a body check changes in the context, so it can be put back. */
typedef struct {
    const TypeEnv *env;
    List *mapping;
    List *locals;
    bool in_function;
    size_t loop_depth;
    size_t defer_depth;
    const char *function_name;
    size_t narrowings;
    size_t match_scope;
} CheckFrame;

static CheckFrame save_frame(const CheckContext *ctx)
{
    return (CheckFrame){ctx->env,        ctx->mapping,       ctx->locals,
                        ctx->in_function, ctx->loop_depth,    ctx->defer_depth,
                        ctx->function_name, ctx->narrowings->length,
                        ctx->match_scope};
}

static void restore_frame(CheckContext *ctx, const CheckFrame *frame)
{
    ctx->match_scope = frame->match_scope;
    ctx->mapping = frame->mapping;
    ctx->env = frame->env;
    ctx->locals = frame->locals;
    ctx->in_function = frame->in_function;
    ctx->loop_depth = frame->loop_depth;
    ctx->defer_depth = frame->defer_depth;
    ctx->function_name = frame->function_name;
    ctx->narrowings->length = frame->narrowings;
}

/* The body gets its own map from syntax nodes to typed nodes, because the
   same syntax is checked again for every instantiation. */
static void open_frame(CheckContext *ctx, const FunctionEntry *entry,
                       const TypeEnv *env, List *mapping, List *locals)
{
    size_t none = TYPED_NO_NODE;
    size_t i;

    ctx->match_scope = ctx->program->match_scopes.length;
    checker_ok(ctx->errors, list_push(&ctx->program->match_scopes, &none),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    for (i = 0; i < ctx->ast->nodes.length && !ctx->errors->fatal; i++) {
        checker_ok(ctx->errors, list_push(mapping, &none),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    ctx->mapping = mapping;
    ctx->env = env;
    ctx->in_function = true;
    ctx->locals = locals;
    ctx->loop_depth = 0;
    ctx->defer_depth = 0;
    ctx->function_name = entry->is_method || entry->is_static
                             ? entry->method_name
                             : entry->name;
    ctx->narrowings->length = 0;
}

static bool emit_instance(CheckContext *ctx, const AstNode *declaration,
                          size_t name_index, size_t function_type,
                          size_t return_type, const List *parameter_nodes)
{
    TypedNode typed = {0};
    size_t typed_body;
    size_t typed_index;
    size_t i;

    if (!check_block(ctx, declaration->left_child, return_type, &typed_body)) {
        return false;
    }
    if (declaration->right_child != AST_NO_NODE &&
        !typed_block_returns(ctx->program, typed_body)) {
        error_report_at(ctx->errors, declaration->pos,
                        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN,
                        ctx->function_name);
        return false;
    }
    typed.children_start = ctx->program->children.length;
    for (i = 0; i < parameter_nodes->length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       ctx->program,
                       *(const size_t *)list_get(parameter_nodes, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    }
    typed.kind = TYPED_FUNCTION_DECLARATION;
    typed.integer_value_u = ctx->match_scope;
    typed.type_index = function_type;
    typed.pos = declaration->pos;
    typed.name_index = name_index;
    typed.left_child = typed_body;
    typed.right_child = TYPED_NO_NODE;
    typed.is_method = declaration->is_method;
    typed.children_count = declaration->children_count;
    if (!checker_push_node(ctx, &typed, &typed_index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    return checker_ok(ctx->errors,
                      list_push(&ctx->program->roots, &typed_index),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

bool check_generic_body(CheckContext *ctx, size_t entry_index, size_t name_index,
                        const TypeEnv *env, size_t function_type,
                        size_t return_type)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    CheckFrame frame = save_frame(ctx);
    List mapping;
    List locals;
    List parameter_nodes;
    bool ok;

    if (declaration == NULL ||
        !check_type_value_size(ctx, return_type, declaration->pos)) {
        return false;
    }
    list_init(&mapping, sizeof(size_t));
    list_init(&locals, sizeof(VariableEntry));
    list_init(&parameter_nodes, sizeof(size_t));
    open_frame(ctx, entry, env, &mapping, &locals);
    ok = declare_parameters(ctx, declaration, &parameter_nodes, NULL) &&
         emit_instance(ctx, declaration, name_index, function_type, return_type,
                       &parameter_nodes);
    restore_frame(ctx, &frame);
    list_free(&mapping);
    list_free(&locals);
    list_free(&parameter_nodes);
    return ok;
}

/* ---- a call ---- */

typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    size_t ast_index;
    size_t function_index;
    const FunctionEntry *entry;
    const AstNode *declaration;
    const char *display_name;
    bool is_method;
    InferEnv infer;
    TypeEnv env;
    List values;          /* the checked arguments, receiver first */
    List parameter_types; /* the parameters' types under `env` */
    size_t return_type;
    size_t function_type;
} GenericCall;

/* The call has the right shape: arguments by position, one per parameter, and
   a receiver that a method can change in place. */
/* The call has the right shape: arguments by position, one per parameter. */
static bool check_call_shape(const GenericCall *g)
{
    CheckContext *ctx = g->ctx;
    const AstNode *node = g->node;
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        const AstNode *argument = ast_get_node(
            ctx->ast, *(const size_t *)list_get(&ctx->ast->children,
                                                node->children_start + i));

        if (argument->kind == AST_NAMED_ARG) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
            return false;
        }
    }
    if (node->children_count + (g->is_method ? 1u : 0u) !=
        g->declaration->children_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, g->display_name,
                        g->declaration->children_count - (g->is_method ? 1u : 0u),
                        node->children_count);
        return false;
    }
    return true;
}

static bool check_generic_receiver(const GenericCall *g)
{
    CheckContext *ctx = g->ctx;

    if (!g->is_method) {
        return true;
    }
    if (!field_is_lvalue(ctx->ast, g->node->left_child)) {
        error_report_at(ctx->errors, g->node->pos,
                        MESSAGE_CHECKER_METHOD_RECEIVER, g->entry->method_name);
        return false;
    }
    return check_inline_const_mutation(ctx, g->node->left_child) &&
           check_const_method_receiver(ctx, g->node->left_child);
}

/* A static function of a generic type is called as Type<T>.f(): the type
   arguments written there bind the type's own parameters. */
static bool bind_owner_type(GenericCall *g)
{
    CheckContext *ctx = g->ctx;
    const FunctionEntry *entry = g->entry;
    const TypedType *owner_type;
    size_t owner_type_index;
    size_t i;

    if (!entry->is_static || entry->owner_parameter_count == 0) {
        return true;
    }
    if (g->node->owner_type_index == AST_NO_NODE) {
        error_report_at(ctx->errors, g->node->pos,
                        MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS,
                        entry->owner_name);
        return false;
    }
    if (!resolve_type_in_env(ctx, g->node->owner_type_index, ctx->env,
                             entry->owner_name, &owner_type_index)) {
        return false;
    }
    owner_type = typed_program_type(ctx->program, owner_type_index);
    if (owner_type == NULL || owner_type->kind != NYX_TYPE_STRUCT ||
        owner_type->child_count != entry->owner_parameter_count) {
        error_report_at(ctx->errors, g->node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < entry->owner_parameter_count; i++) {
        g->infer.types[i] = *(const size_t *)list_get(
            &ctx->program->type_children, owner_type->first_child + i);
    }
    return true;
}

static bool open_generic_call(GenericCall *g)
{
    size_t i;

    if (!check_call_shape(g) || !check_generic_receiver(g)) {
        return false;
    }
    for (i = 0; i < g->entry->parameter_count; i++) {
        g->infer.names[i] = g->entry->parameters[i];
        g->infer.types[i] = TYPED_NO_NODE;
    }
    g->infer.count = g->entry->parameter_count;
    return bind_owner_type(g);
}

/* The type parameter a declared parameter type names, if it is one. */
static size_t bound_parameter(const GenericCall *g, const AstNode *type_node)
{
    const char *name = type_node != NULL && type_node->kind == AST_TYPE_NAME
                           ? ast_symbol_text(g->ctx->ast, type_node->name_index)
                           : NULL;

    return name == NULL ? TYPED_NO_NODE : infer_find(&g->infer, name);
}

static bool names_store(const GenericCall *g, const AstNode *type_node)
{
    const char *name = type_node != NULL && type_node->kind == AST_TYPE_NAME
                           ? ast_symbol_text(g->ctx->ast, type_node->name_index)
                           : NULL;

    return name != NULL && strcmp(name, "Store") == 0;
}

/* Check one argument, expecting the type its parameter has so far. */
static bool check_generic_argument(GenericCall *g, const AstNode *type_node,
                                   size_t bound, size_t argument_ast,
                                   size_t *value)
{
    CheckContext *ctx = g->ctx;
    size_t expected = bound == TYPED_NO_NODE ? TYPED_NO_NODE
                                             : g->infer.types[bound];

    if (names_store(g, type_node) &&
        !builtin_type(ctx, NYX_TYPE_STORE, &expected)) {
        return false;
    }
    if (!check_expression_expected(ctx, argument_ast, expected, value)) {
        return false;
    }
    return bound == TYPED_NO_NODE || g->infer.types[bound] != TYPED_NO_NODE ||
           check_default_numeric(ctx, *value);
}

/* A string literal does not say which string type its parameter is. When the
   call's own result has that type and the context expects a string type, it
   is taken from the context; otherwise inference waits for another argument.
   Returns true when inference must wait. */
/* A string literal whose type no other argument has fixed yet. */
static bool is_unfixed_string(const GenericCall *g, const TypedNode *value_node,
                              size_t bound)
{
    return value_node != NULL && value_node->kind == TYPED_STRING_LITERAL &&
           !value_node->fixed_type && bound != TYPED_NO_NODE &&
           g->infer.types[bound] == TYPED_NO_NODE;
}

/* Whether the call's result has the parameter's type and the context expects a
   string type of it. */
static bool context_gives_string_type(const GenericCall *g,
                                      const AstNode *type_node)
{
    const CheckContext *ctx = g->ctx;
    const AstNode *result = ast_get_node(ctx->ast, g->declaration->right_child);
    const TypedType *expected =
        ctx->expected_ast == g->ast_index
            ? typed_program_type(ctx->program, ctx->expected_type)
            : NULL;

    return result != NULL && result->kind == AST_TYPE_NAME &&
           result->name_index == type_node->name_index && expected != NULL &&
           (expected->kind == NYX_TYPE_STRING ||
            expected->kind == NYX_TYPE_DYNAMIC_STRING);
}

/* A string literal does not say which string type its parameter is. When the
   call's own result has that type and the context expects a string type, it
   is taken from the context; otherwise inference waits for another argument.
   Returns true when inference must wait. */
static bool defers_string_inference(GenericCall *g, const TypedNode *value_node,
                                    size_t bound, const AstNode *type_node)
{
    if (!is_unfixed_string(g, value_node, bound)) {
        return false;
    }
    if (!context_gives_string_type(g, type_node)) {
        return true;
    }
    g->infer.types[bound] = g->ctx->expected_type;
    return false;
}

static size_t generic_argument_ast(const GenericCall *g, size_t i)
{
    if (g->is_method && i == 0) {
        return g->node->left_child;
    }
    return *(const size_t *)list_get(&g->ctx->ast->children,
                                     g->node->children_start + i -
                                         (g->is_method ? 1u : 0u));
}

/* Check the argument for parameter `i` and learn the type parameters it
   fixes. */
static bool infer_from_argument(GenericCall *g, size_t i)
{
    CheckContext *ctx = g->ctx;
    size_t parameter_ast = *(const size_t *)list_get(
        &ctx->ast->children, g->declaration->children_start + i);
    const AstNode *parameter = ast_get_node(ctx->ast, parameter_ast);
    const AstNode *type_node;
    const TypedNode *value_node;
    size_t bound;
    size_t value;
    bool wait;

    if (parameter == NULL || parameter->kind != AST_PARAMETER) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                     parameter_ast);
        return false;
    }
    type_node = ast_get_node(ctx->ast, parameter->left_child);
    bound = bound_parameter(g, type_node);
    if (!check_generic_argument(g, type_node, bound, generic_argument_ast(g, i),
                                &value)) {
        return false;
    }
    value_node = typed_program_node(ctx->program, value);
    wait = defers_string_inference(g, value_node, bound, type_node);
    if (value_node == NULL ||
        (!wait && ast_type_has_unbound(ctx, parameter->left_child, &g->infer,
                                       NYX_TYPE_DEPTH) &&
         !infer_ast_type(ctx, parameter->left_child, value_node->type_index,
                         &g->infer, g->display_name))) {
        error_report_at(ctx->errors, g->node->pos,
                        MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                        infer_first_unbound(&g->infer), g->display_name);
        return false;
    }
    checker_ok(ctx->errors, list_push(&g->values, &value),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

/* Every type parameter must be fixed, and none may be a Store. */
static bool finish_inference(GenericCall *g)
{
    CheckContext *ctx = g->ctx;
    size_t i;

    if (!infer_finish(ctx, &g->infer, &g->env, g->display_name)) {
        return false;
    }
    for (i = 0; i < g->env.count; i++) {
        const TypedType *type = typed_program_type(ctx->program, g->env.types[i]);

        if (type != NULL && type->kind == NYX_TYPE_STORE) {
            error_report_at(ctx->errors, g->node->pos,
                            MESSAGE_CHECKER_STORE_TYPE);
            return false;
        }
    }
    return true;
}

static bool infer_types(GenericCall *g)
{
    size_t i;

    for (i = 0; i < g->declaration->children_count; i++) {
        if (!infer_from_argument(g, i)) {
            return false;
        }
    }
    return finish_inference(g);
}

/* The instantiation's parameter, return and function types. */
static bool resolve_instance_types(GenericCall *g)
{
    CheckContext *ctx = g->ctx;
    const AstNode *declaration = g->declaration;
    const TypedType *resolved;
    size_t i;

    for (i = 0; i < declaration->children_count; i++) {
        const AstNode *parameter = ast_get_node(
            ctx->ast, *(const size_t *)list_get(&ctx->ast->children,
                                                declaration->children_start + i));
        size_t parameter_type;

        if (!resolve_type_in_env(ctx, parameter->left_child, &g->env,
                                 g->display_name, &parameter_type)) {
            return false;
        }
        checker_ok(ctx->errors, list_push(&g->parameter_types, &parameter_type),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (!(declaration->right_child == AST_NO_NODE
              ? builtin_type(ctx, NYX_TYPE_NIL, &g->return_type)
              : resolve_type_in_env(ctx, declaration->right_child, &g->env,
                                    g->display_name, &g->return_type))) {
        return false;
    }
    resolved = typed_program_type(ctx->program, g->return_type);
    if (resource_type_message(resolved) != NULL) {
        error_report_at(ctx->errors, declaration->pos,
                        resource_type_message(resolved));
        return false;
    }
    return add_function_type(ctx, &g->parameter_types, g->return_type,
                             &g->function_type);
}

/* The first call with a new set of types creates the instance: a name, a
   record, and a checked body. */
static bool create_instance(GenericCall *g, FunctionInstantiation **record)
{
    CheckContext *ctx = g->ctx;
    FunctionInstantiation fresh = {0};
    char generated[NYX_IDENTIFIER_MAX];
    size_t i;

    if (ctx->function_instantiations->length >= NYX_MAX_INSTANTIATIONS) {
        error_report(ctx->errors, MESSAGE_CHECKER_TOO_MANY_INSTANTIATIONS,
                     g->display_name, NYX_MAX_INSTANTIATIONS);
        return false;
    }
    fresh.entry = g->function_index;
    fresh.argument_count = g->env.count;
    for (i = 0; i < g->env.count; i++) {
        fresh.arguments[i] = g->env.types[i];
    }
    fresh.type_index = g->function_type;
    fresh.in_progress = true;
    snprintf(generated, sizeof(generated), "nyx_genfn_%zu",
             ctx->generated_names++);
    fresh.name_index = checker_name(ctx, generated);
    if (ctx->errors->fatal ||
        !checker_ok(ctx->errors,
                    list_push(ctx->function_instantiations, &fresh),
                    MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    i = ctx->function_instantiations->length - 1;
    if (!check_generic_body(ctx, g->function_index, fresh.name_index, &g->env,
                            g->function_type, g->return_type)) {
        return false;
    }
    *record = (FunctionInstantiation *)list_get(ctx->function_instantiations, i);
    (*record)->in_progress = false;
    (*record)->emitted = true;
    return true;
}

static bool find_or_create_instance(GenericCall *g,
                                    FunctionInstantiation **record)
{
    *record = find_function_instantiation(g->ctx, g->function_index,
                                          g->env.types, g->env.count);
    return *record != NULL || create_instance(g, record);
}

/* Coerce every argument to the instance's parameter types. */
static bool coerce_arguments(const GenericCall *g,
                             const TypedFunctionSignature *signature,
                             List *coerced)
{
    CheckContext *ctx = g->ctx;
    size_t first_parameter = signature->first_parameter;
    size_t i;

    for (i = 0; i < g->values.length; i++) {
        size_t value = *(const size_t *)list_get(&g->values, i);
        size_t parameter_type = *(const size_t *)list_get(
            &ctx->program->signature_parameters, first_parameter + i);
        size_t injected;

        if (!inject_into(ctx, value, parameter_type, &injected)) {
            if (!ctx->errors->reported) {
                error_report_at(
                    ctx->errors, g->node->pos,
                    g->is_method && i == 0 ? MESSAGE_CHECKER_METHOD_RECEIVER
                                           : MESSAGE_CHECKER_FUNCTION_ARG_TYPE,
                    i + 1 - (g->is_method ? 1u : 0u), g->display_name);
            }
            return false;
        }
        checker_ok(ctx->errors, list_push(coerced, &injected),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return true;
}

/* The call of an instance: the arguments coerced to its signature. */
static bool emit_instance_call(const GenericCall *g,
                               const FunctionInstantiation *record)
{
    CheckContext *ctx = g->ctx;
    const TypedType *callee = typed_program_type(ctx->program, record->type_index);
    const TypedFunctionSignature *signature =
        callee == NULL || callee->kind != NYX_TYPE_FUNCTION
            ? NULL
            : typed_program_signature(ctx->program, callee->signature_index);
    size_t name_index = record->name_index;
    size_t return_type;
    List coerced;
    bool ok;

    if (signature == NULL || signature->parameter_count != g->values.length) {
        error_report_at(ctx->errors, g->node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    return_type = signature->return_type;
    list_init(&coerced, sizeof(size_t));
    ok = coerce_arguments(g, signature, &coerced) &&
         emit_call_to(ctx, g->node, name_index, g->ast_index, return_type,
                      &coerced, g->is_method);
    list_free(&coerced);
    return ok;
}

static bool run_generic_call(GenericCall *g)
{
    FunctionInstantiation *record;

    return open_generic_call(g) && infer_types(g) && resolve_instance_types(g) &&
           find_or_create_instance(g, &record) && emit_instance_call(g, record);
}

bool check_generic_call(CheckContext *ctx, const AstNode *node,
                        size_t function_index, size_t ast_index)
{
    const FunctionEntry *entry =
        (const FunctionEntry *)list_get(ctx->functions, function_index);
    GenericCall g = {0};
    bool ok;

    g.ctx = ctx;
    g.node = node;
    g.ast_index = ast_index;
    g.function_index = function_index;
    g.entry = entry;
    g.declaration = ast_get_node(ctx->ast, entry->ast_index);
    g.display_name = entry->is_method || entry->is_static ? entry->method_name
                                                          : entry->name;
    g.is_method = entry->is_method;
    list_init(&g.values, sizeof(size_t));
    list_init(&g.parameter_types, sizeof(size_t));
    ok = run_generic_call(&g);
    list_free(&g.values);
    list_free(&g.parameter_types);
    return ok;
}
