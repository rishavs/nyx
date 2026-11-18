#include "checker_internal.h"


size_t infer_find(const InferEnv *infer, const char *name)
{
    size_t i;

    for (i = 0; i < infer->count; i++) {
        if (infer->names[i] != NULL && strcmp(infer->names[i], name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}


void infer_type_env(const InferEnv *infer, TypeEnv *env)
{
    size_t i;

    env->count = 0;
    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] != TYPED_NO_NODE) {
            env->names[env->count] = infer->names[i];
            env->types[env->count] = infer->types[i];
            env->count++;
        }
    }
}

static bool any_child_unbound(CheckContext *ctx, const AstNode *node,
                              const InferEnv *infer, unsigned depth)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);

        if (ast_type_has_unbound(ctx, child, infer, depth - 1)) {
            return true;
        }
    }
    /* A function type's result is its right operand. */
    return node->kind == AST_TYPE_FUNCTION && node->right_child != AST_NO_NODE &&
           ast_type_has_unbound(ctx, node->right_child, infer, depth - 1);
}

/* The kinds of written type that can contain a type parameter among their
   parts. */
static bool has_parts(AstNodeKind kind)
{
    return kind == AST_TYPE_UNION || kind == AST_TYPE_FUNCTION ||
           kind == AST_TYPE_APPLICATION;
}

/* The type parameter an application or name refers to, if any. */
static size_t parameter_named(CheckContext *ctx, const AstNode *node,
                              const InferEnv *infer)
{
    const char *name = node->kind == AST_TYPE_NAME || node->kind == AST_TYPE_APPLICATION
                           ? ast_symbol_text(ctx->ast, node->name_index)
                           : NULL;

    return name == NULL ? TYPED_NO_NODE : infer_find(infer, name);
}

/* Whether a type written in a generic signature still mentions a type
   parameter that has not been bound to a type yet. */
bool ast_type_has_unbound(CheckContext *ctx, size_t ast_index,
                          const InferEnv *infer, unsigned depth)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t index;

    if (node == NULL || depth == 0) {
        return true;
    }
    index = parameter_named(ctx, node, infer);
    if (node->kind == AST_TYPE_NAME) {
        return index != TYPED_NO_NODE && infer->types[index] == TYPED_NO_NODE;
    }
    return has_parts(node->kind) &&
           ((node->kind == AST_TYPE_APPLICATION && index != TYPED_NO_NODE) ||
            any_child_unbound(ctx, node, infer, depth));
}

/* Matching a type written in a generic signature against the concrete type of
   an argument binds the signature's type parameters. Which syntax the written
   type uses picks the rule. */
typedef struct {
    CheckContext *ctx;
    size_t ast_type;
    const AstNode *node;
    size_t concrete;
    const TypedType *concrete_type;
    InferEnv *infer;
    const char *owner;
} Inference;

static bool infer_ast_type_in(const Inference *in, size_t ast_type,
                              size_t concrete)
{
    return infer_ast_type(in->ctx, ast_type, concrete, in->infer, in->owner);
}

/* A written type with nothing left to bind is the type it names. */
static bool names_concrete(const Inference *in)
{
    TypeEnv env = {0};
    size_t expected;

    infer_type_env(in->infer, &env);
    return resolve_type_in_env(in->ctx, in->ast_type, &env, in->owner,
                               &expected) &&
           same_type(in->ctx->program, expected, in->concrete);
}

static bool infer_name(const Inference *in)
{
    const char *name = ast_symbol_text(in->ctx->ast, in->node->name_index);
    size_t index = name == NULL ? TYPED_NO_NODE : infer_find(in->infer, name);

    if (index == TYPED_NO_NODE) {
        return names_concrete(in);
    }
    if (in->infer->types[index] == TYPED_NO_NODE) {
        in->infer->types[index] = in->concrete;
        return true;
    }
    return same_type(in->ctx->program, in->infer->types[index], in->concrete);
}

/* Handle<T> and Range<T> match a Handle or Range of a type that matches T. */
static bool infer_wrapper(const Inference *in, NyxType kind)
{
    CheckContext *ctx = in->ctx;

    if (in->concrete_type->kind != kind || in->concrete_type->child_count != 1 ||
        in->node->children_count != 1) {
        return false;
    }
    return infer_ast_type_in(
        in,
        *(const size_t *)list_get(&ctx->ast->children, in->node->children_start),
        *(const size_t *)list_get(&ctx->program->type_children,
                                  in->concrete_type->first_child));
}

/* A generic struct applied to type arguments matches an instance of that
   struct whose arguments match. */
static bool infer_struct_application(const Inference *in, const TypeEntry *entry)
{
    CheckContext *ctx = in->ctx;
    const TypedType *type = in->concrete_type;
    const char *source_name =
        type->source_name_index == TYPED_NO_NODE
            ? NULL
            : typed_program_symbol_text(ctx->program, type->source_name_index);
    size_t i;

    if (type->kind != NYX_TYPE_STRUCT || source_name == NULL ||
        strcmp(source_name, entry->name) != 0 ||
        type->child_count != in->node->children_count) {
        return false;
    }
    for (i = 0; i < in->node->children_count; i++) {
        if (!infer_ast_type_in(
                in,
                *(const size_t *)list_get(&ctx->ast->children,
                                          in->node->children_start + i),
                *(const size_t *)list_get(&ctx->program->type_children,
                                          type->first_child + i))) {
            return false;
        }
    }
    return true;
}

static bool infer_application(const Inference *in)
{
    const char *name = ast_symbol_text(in->ctx->ast, in->node->name_index);
    const TypeEntry *entry;

    if (name == NULL || infer_find(in->infer, name) != TYPED_NO_NODE) {
        return false;
    }
    if (strcmp(name, "Handle") == 0) {
        return infer_wrapper(in, NYX_TYPE_HANDLE);
    }
    if (strcmp(name, "Range") == 0) {
        return infer_wrapper(in, NYX_TYPE_RANGE);
    }
    entry = find_type(in->ctx->types, name);
    if (entry == NULL) {
        return false;
    }
    if (entry->kind == TYPE_ENTRY_STRUCT) {
        return infer_struct_application(in, entry);
    }
    return !ast_type_has_unbound(in->ctx, in->ast_type, in->infer,
                                 NYX_TYPE_DEPTH) &&
           names_concrete(in);
}

static bool infer_function(const Inference *in)
{
    CheckContext *ctx = in->ctx;
    const TypedFunctionSignature *signature;
    const TypedType *resolved;
    size_t first_parameter;
    size_t return_type;
    size_t i;

    if (in->concrete_type->kind != NYX_TYPE_FUNCTION) {
        return false;
    }
    signature = typed_program_signature(ctx->program,
                                        in->concrete_type->signature_index);
    if (signature == NULL || signature->parameter_count != in->node->children_count) {
        return false;
    }
    first_parameter = signature->first_parameter;
    return_type = signature->return_type;
    for (i = 0; i < in->node->children_count; i++) {
        if (!infer_ast_type_in(
                in,
                *(const size_t *)list_get(&ctx->ast->children,
                                          in->node->children_start + i),
                *(const size_t *)list_get(&ctx->program->signature_parameters,
                                          first_parameter + i))) {
            return false;
        }
    }
    if (in->node->right_child != AST_NO_NODE) {
        return infer_ast_type_in(in, in->node->right_child, return_type);
    }
    resolved = typed_program_type(ctx->program, return_type);
    return resolved != NULL && resolved->kind == NYX_TYPE_NIL;
}

/* ---- a union with one parameter among its alternatives ---- */

/* The parameter an alternative of the written union is, when it is an unbound
   one. */
static size_t unbound_parameter(const Inference *in, size_t child)
{
    const AstNode *part = ast_get_node(in->ctx->ast, child);
    const char *name = part != NULL && part->kind == AST_TYPE_NAME
                           ? ast_symbol_text(in->ctx->ast, part->name_index)
                           : NULL;
    size_t parameter = name == NULL ? TYPED_NO_NODE : infer_find(in->infer, name);

    return parameter != TYPED_NO_NODE &&
                   in->infer->types[parameter] == TYPED_NO_NODE
               ? parameter
               : TYPED_NO_NODE;
}

/* The alternatives that are already known, and the one unbound parameter.
   Returns false when there is more than one, or an alternative that cannot be
   resolved yet. */
static bool classify_alternatives(const Inference *in, size_t *unbound,
                                  List *fixed)
{
    CheckContext *ctx = in->ctx;
    size_t i;

    *unbound = TYPED_NO_NODE;
    for (i = 0; i < in->node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 in->node->children_start + i);
        size_t parameter = unbound_parameter(in, child);
        TypeEnv env = {0};
        size_t resolved;

        if (parameter != TYPED_NO_NODE) {
            if (*unbound != TYPED_NO_NODE) {
                return false;
            }
            *unbound = parameter;
            continue;
        }
        infer_type_env(in->infer, &env);
        if (ast_type_has_unbound(ctx, child, in->infer, NYX_TYPE_DEPTH) ||
            !resolve_type_in_env(ctx, child, &env, in->owner, &resolved)) {
            return false;
        }
        checker_ok(ctx->errors, list_push(fixed, &resolved),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return *unbound != TYPED_NO_NODE && !ctx->errors->fatal;
}

/* The alternatives of the concrete type: its own if it is a union, else it. */
static bool concrete_alternatives(const Inference *in, List *actuals)
{
    CheckContext *ctx = in->ctx;
    bool is_union = in->concrete_type->kind == NYX_TYPE_UNION;
    size_t count = is_union ? in->concrete_type->child_count : 1;
    size_t i;

    for (i = 0; i < count; i++) {
        size_t actual =
            is_union ? *(const size_t *)list_get(
                           &ctx->program->type_children,
                           in->concrete_type->first_child + i)
                     : in->concrete;

        checker_ok(ctx->errors, list_push(actuals, &actual),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

static bool is_covered(const CheckContext *ctx, const List *fixed, size_t actual)
{
    size_t j;

    for (j = 0; j < fixed->length; j++) {
        if (same_type(ctx->program, actual, *(const size_t *)list_get(fixed, j))) {
            return true;
        }
    }
    return false;
}

/* The parameter takes whatever the known alternatives do not cover. */
static bool bind_uncovered(const Inference *in, size_t unbound,
                           const List *actuals, const List *fixed,
                           List *residual)
{
    CheckContext *ctx = in->ctx;
    size_t i;

    for (i = 0; i < actuals->length; i++) {
        size_t actual = *(const size_t *)list_get(actuals, i);

        if (!is_covered(ctx, fixed, actual)) {
            checker_ok(ctx->errors, list_push(residual, &actual),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    if (ctx->errors->fatal) {
        return false;
    }
    if (residual->length == 1) {
        in->infer->types[unbound] = *(const size_t *)list_get(residual, 0);
        return true;
    }
    /* Nothing left over: a later argument can still determine the parameter. */
    return residual->length == 0 ||
           intern_union(ctx, residual, &in->infer->types[unbound]);
}

static bool infer_open_union(const Inference *in)
{
    List actuals;
    List fixed;
    List residual;
    size_t unbound;
    bool ok;

    list_init(&actuals, sizeof(size_t));
    list_init(&fixed, sizeof(size_t));
    list_init(&residual, sizeof(size_t));
    ok = concrete_alternatives(in, &actuals) &&
         classify_alternatives(in, &unbound, &fixed) &&
         bind_uncovered(in, unbound, &actuals, &fixed, &residual);
    list_free(&actuals);
    list_free(&fixed);
    list_free(&residual);
    return ok;
}

static bool infer_union(const Inference *in)
{
    return ast_type_has_unbound(in->ctx, in->ast_type, in->infer,
                                NYX_TYPE_DEPTH)
               ? infer_open_union(in)
               : names_concrete(in);
}

static const struct {
    AstNodeKind kind;
    bool (*infer)(const Inference *in);
} inferrers[] = {
    {AST_TYPE_NAME, infer_name},
    {AST_TYPE_APPLICATION, infer_application},
    {AST_TYPE_UNION, infer_union},
    {AST_TYPE_FUNCTION, infer_function},
};

bool infer_ast_type(CheckContext *ctx, size_t ast_type, size_t concrete,
                    InferEnv *infer, const char *owner)
{
    Inference in = {ctx, ast_type, ast_get_node(ctx->ast, ast_type), concrete,
                    typed_program_type(ctx->program, concrete), infer, owner};
    size_t i;

    if (in.node == NULL || in.concrete_type == NULL) {
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(inferrers); i++) {
        if (inferrers[i].kind == in.node->kind) {
            return inferrers[i].infer(&in);
        }
    }
    return false;
}


const char *infer_first_unbound(const InferEnv *infer)
{
    size_t i;

    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] == TYPED_NO_NODE) {
            return infer->names[i];
        }
    }
    return infer->count == 0 ? "?" : infer->names[0];
}


bool infer_finish(CheckContext *ctx, InferEnv *infer, TypeEnv *env,
                         const char *owner)
{
    size_t i;

    env->count = 0;
    for (i = 0; i < infer->count; i++) {
        if (infer->types[i] == TYPED_NO_NODE) {
            error_report(ctx->errors, MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                         infer->names[i], owner);
            return false;
        }
        {
            const TypedType *type = typed_program_type(ctx->program,
                                                       infer->types[i]);

            if (type != NULL && type->kind == NYX_TYPE_NIL) {
                error_report(ctx->errors, MESSAGE_CHECKER_NIL_TYPE);
                return false;
            }
        }
        env->names[env->count] = infer->names[i];
        env->types[env->count] = infer->types[i];
        env->count++;
    }
    return true;
}


bool constructor_value_fits(CheckContext *ctx, size_t value,
                                   const TypeEntry *entry, size_t field_index,
                                   size_t argument_index, SourcePos pos,
                                   size_t *out_value)
{
    size_t field_type_index = struct_field_type(ctx->program,
                                                typed_program_node(
                                                    ctx->program,
                                                    struct_declaration_node(
                                                        ctx, entry)),
                                                field_index);

    if (!inject_into(ctx, value, field_type_index, out_value)) {
        error_report_at(ctx->errors, pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE,
                        argument_index, entry->name);
        return false;
    }
    return true;
}


const AstNode *template_field(const CheckContext *ctx,
                                     const AstNode *declaration, size_t index,
                                     size_t *out_ast)
{
    size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                             declaration->children_start +
                                                 index);
    if (out_ast != NULL) {
        *out_ast = child;
    }
    return ast_get_node(ctx->ast, child);
}


size_t template_field_index(const CheckContext *ctx,
                                   const AstNode *declaration,
                                   const char *name)
{
    size_t i;

    for (i = 0; i < declaration->children_count; i++) {
        const AstNode *field = template_field(ctx, declaration, i, NULL);
        const char *field_name;

        if (field == NULL) {
            continue;
        }
        field_name = ast_symbol_text(ctx->ast, field->name_index);
        if (field_name != NULL && strcmp(field_name, name) == 0) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}


const char *struct_field_name(const TypedProgram *program,
                                     const TypedNode *type_decl,
                                     size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? NULL
                         : typed_program_symbol_text(program, field->name_index);
}


size_t struct_field_type(const TypedProgram *program,
                                const TypedNode *type_decl,
                                size_t field_index)
{
    size_t child = *(const size_t *)list_get(
        &program->children, type_decl->children_start + field_index);
    const TypedNode *field = typed_program_node(program, child);

    return field == NULL ? TYPED_NO_NODE : field->type_index;
}


size_t struct_declaration_node(const CheckContext *ctx,
                                    const TypeEntry *entry)
{
    const TypedType *type;

    if (entry == NULL || entry->type_index == TYPED_NO_NODE) {
        return TYPED_NO_NODE;
    }
    type = typed_program_type(ctx->program, entry->type_index);
    return type == NULL ? TYPED_NO_NODE : type->declaration_node;
}
