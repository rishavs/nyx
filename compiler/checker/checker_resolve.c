#include "checker_internal.h"

/* Resolving a type written in the source to a type in the typed program, under
   an environment that binds the type parameters of the declaration being
   checked. Which kind of syntax a type is written with picks the resolver. */

/* ---- structs ---- */

/* A type that cannot be the argument of a generic type. */
static bool check_instantiation_arguments(CheckContext *ctx,
                                          const size_t *arguments, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const char *message = resource_type_message(
            typed_program_type(ctx->program, arguments[i]));

        if (message != NULL) {
            error_report(ctx->errors, message);
            return false;
        }
    }
    return true;
}

static bool same_arguments(const CheckContext *ctx,
                           const StructInstantiation *record,
                           const size_t *arguments, size_t count)
{
    size_t j;

    for (j = 0; j < count; j++) {
        if (!same_type(ctx->program, record->arguments[j], arguments[j])) {
            return false;
        }
    }
    return true;
}

/* An instantiation already made (or being made) for these arguments. */
static StructInstantiation *find_instantiation(CheckContext *ctx,
                                               size_t entry_index,
                                               const size_t *arguments,
                                               size_t count)
{
    size_t i;

    for (i = 0; i < ctx->struct_instantiations->length; i++) {
        StructInstantiation *record =
            (StructInstantiation *)list_get(ctx->struct_instantiations, i);

        if (record->entry == entry_index && record->argument_count == count &&
            same_arguments(ctx, record, arguments, count)) {
            return record;
        }
    }
    return NULL;
}

/* A field holds a value: not a function, and not a resource. */
static bool field_type_allowed(CheckContext *ctx, const AstNode *field,
                               const char *name, size_t type)
{
    const TypedType *resolved = typed_program_type(ctx->program, type);

    if (resolved != NULL && (resolved->kind == NYX_TYPE_FUNCTION ||
                             resource_type_message(resolved) != NULL)) {
        error_report_at(ctx->errors, field->pos, MESSAGE_CHECKER_FIELD_TYPE,
                        name);
        return false;
    }
    return true;
}

/* A field: its type may not be a function or a resource, and it may carry a
   default that must fit that type. The typed field is appended to
   `field_nodes`. */
/* A field's default value must fit its type. */
static bool check_field_default(CheckContext *ctx, const AstNode *field,
                                const char *name, size_t type)
{
    size_t value;
    size_t coerced;

    if (field->right_child == AST_NO_NODE ||
        (check_expression_expected(ctx, field->right_child, type, &value) &&
         inject_into(ctx, value, type, &coerced))) {
        return true;
    }
    if (!ctx->errors->reported) {
        error_report_at(ctx->errors, field->pos,
                        MESSAGE_CHECKER_FIELD_DEFAULT_TYPE, name);
    }
    return false;
}

/* A field: its type may not be a function or a resource, and it may carry a
   default that must fit that type. The typed field is appended to
   `field_nodes`. */
static bool struct_field(CheckContext *ctx, size_t field_ast, const TypeEnv *env,
                         List *field_nodes)
{
    const AstNode *field = ast_get_node(ctx->ast, field_ast);
    const char *name;
    size_t type;
    size_t index;
    TypedNode typed;

    if (field == NULL || field->kind != AST_FIELD) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, field_ast);
        return false;
    }
    name = ast_symbol_text(ctx->ast, field->name_index);
    if (!check_reserved_name(ctx, field->pos, name) ||
        !resolve_type_in_env(ctx, field->left_child, env, name, &type)) {
        return false;
    }
    if (!field_type_allowed(ctx, field, name, type)) {
        return false;
    }
    typed = new_node(TYPED_FIELD, type, field->pos, TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, name);
    if (!check_field_default(ctx, field, name, type) ||
        !checker_push_node(ctx, &typed, &index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    return checker_ok(ctx->errors, list_push(field_nodes, &index),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
}

/* A struct being instantiated: its record, the environment binding its type
   parameters, and the pieces of the typed struct as they are made. */
typedef struct {
    CheckContext *ctx;
    size_t entry_index;
    const TypeEntry *entry;
    const size_t *arguments;
    size_t argument_count;
    TypeEnv env;
    const AstNode *declaration;
    size_t record_index;
    size_t name_index;
    size_t first_argument;
    size_t type_index;
} Instantiation;

/* Record the instantiation as in progress (so a use of the struct inside its
   own fields is seen), name it, and add its arguments as the type's
   children. */
static bool begin_instantiation(Instantiation *inst)
{
    CheckContext *ctx = inst->ctx;
    StructInstantiation record = {0};
    char generated[32];
    size_t i;

    record.entry = inst->entry_index;
    record.argument_count = inst->argument_count;
    record.in_progress = true;
    record.handle_depth = ctx->handle_depth;
    for (i = 0; i < inst->argument_count; i++) {
        record.arguments[i] = inst->arguments[i];
        inst->env.names[i] = inst->entry->parameters[i];
        inst->env.types[i] = inst->arguments[i];
    }
    inst->env.count = inst->argument_count;
    if (!checker_ok(ctx->errors, list_push(ctx->struct_instantiations, &record),
                    MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING)) {
        return false;
    }
    inst->record_index = ctx->struct_instantiations->length - 1;
    snprintf(generated, sizeof(generated), "nyx_gen_%zu", ctx->generated_names++);
    inst->name_index = checker_name(ctx, generated);
    inst->declaration = ast_get_node(ctx->ast, inst->entry->ast_index);
    if (inst->declaration == NULL ||
        (inst->declaration->kind != AST_TYPE_DECLARATION &&
         inst->declaration->kind != AST_ADT_CASE)) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    inst->first_argument = ctx->program->type_children.length;
    for (i = 0; i < inst->argument_count; i++) {
        checker_ok(ctx->errors,
                   typed_program_add_type_child(ctx->program,
                                                inst->arguments[i]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return true;
}

/* The struct type itself, before its fields, so that a Handle to it inside its
   own fields has something to point at. */
static bool add_struct_type(Instantiation *inst)
{
    TypedType type = {0};
    StructInstantiation *record;

    type.kind = NYX_TYPE_STRUCT;
    type.name_index = inst->name_index;
    type.signature_index = TYPED_NO_NODE;
    type.first_child = inst->first_argument;
    type.child_count = inst->argument_count;
    type.declaration_node = TYPED_NO_NODE;
    type.source_name_index = checker_name(inst->ctx, inst->entry->name);
    if (!checker_add_type(inst->ctx, &type, &inst->type_index)) {
        return false;
    }
    record = (StructInstantiation *)list_get(inst->ctx->struct_instantiations,
                                             inst->record_index);
    record->type_index = inst->type_index;
    return true;
}

/* The fields, then the declaration node that holds them. */
static bool add_struct_declaration(Instantiation *inst)
{
    CheckContext *ctx = inst->ctx;
    const AstNode *declaration = inst->declaration;
    TypedNode typed;
    List field_nodes;
    size_t declaration_index;
    size_t i;
    bool ok = true;

    list_init(&field_nodes, sizeof(size_t));
    for (i = 0; ok && i < declaration->children_count; i++) {
        ok = struct_field(ctx, *(const size_t *)list_get(
                                   &ctx->ast->children,
                                   declaration->children_start + i),
                          &inst->env, &field_nodes);
    }
    typed = new_node(TYPED_TYPE_DECLARATION, TYPED_NO_NODE, declaration->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = inst->name_index;
    typed.children_start = push_typed_children(ctx, &field_nodes);
    typed.children_count = declaration->children_count;
    list_free(&field_nodes);
    if (!ok ||
        !checker_push_node(ctx, &typed, &declaration_index,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    ((TypedType *)list_get(&ctx->program->types, inst->type_index))
        ->declaration_node = declaration_index;
    return true;
}

static bool build_instantiation(Instantiation *inst)
{
    CheckContext *ctx = inst->ctx;
    StructInstantiation *record;

    if (inst->argument_count != inst->entry->parameter_count) {
        error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT,
                     inst->entry->name, inst->entry->parameter_count,
                     inst->argument_count);
        return false;
    }
    if (!begin_instantiation(inst) || !add_struct_type(inst) ||
        !add_struct_declaration(inst)) {
        return false;
    }
    record = (StructInstantiation *)list_get(ctx->struct_instantiations,
                                             inst->record_index);
    record->type_index = inst->type_index;
    record->in_progress = false;
    return check_type_value_size(ctx, inst->type_index, inst->declaration->pos);
}

static bool instantiate_struct_inner(CheckContext *ctx, size_t entry_index,
                                     const size_t *arguments,
                                     size_t argument_count, size_t *out_type)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    StructInstantiation *existing;
    Instantiation inst = {0};

    if (entry == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (!check_instantiation_arguments(ctx, arguments, argument_count)) {
        return false;
    }
    existing = find_instantiation(ctx, entry_index, arguments, argument_count);
    if (existing != NULL) {
        if (existing->in_progress && ctx->handle_depth <= existing->handle_depth) {
            error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE, entry->name);
            return false;
        }
        *out_type = existing->type_index;
        return true;
    }
    inst.ctx = ctx;
    inst.entry_index = entry_index;
    inst.entry = entry;
    inst.arguments = arguments;
    inst.argument_count = argument_count;
    if (!build_instantiation(&inst)) {
        return false;
    }
    *out_type = inst.type_index;
    return true;
}

bool instantiate_struct(CheckContext *ctx, size_t entry_index,
                        const size_t *arguments, size_t argument_count,
                        size_t *out_type)
{
    bool ok;

    if (ctx->struct_depth >= NYX_TYPE_DEPTH) {
        error_report(ctx->errors, MESSAGE_CHECKER_TYPE_INSTANTIATION_DEPTH,
                     (size_t)NYX_TYPE_DEPTH);
        return false;
    }
    ctx->struct_depth++;
    ok = instantiate_struct_inner(ctx, entry_index, arguments, argument_count,
                                  out_type);
    ctx->struct_depth--;
    return ok;
}

/* ---- aliases ---- */

bool expand_alias(CheckContext *ctx, const TypeEntry *entry,
                  const size_t *arguments, size_t argument_count,
                  size_t *out_type)
{
    TypeEnv env = {0};
    const AstNode *alias;
    size_t i;
    bool ok;

    if (argument_count != entry->parameter_count) {
        error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT,
                     entry->name, entry->parameter_count, argument_count);
        return false;
    }
    alias = ast_get_node(ctx->ast, entry->ast_index);
    if (alias == NULL || alias->kind != AST_TYPE_ALIAS) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    for (i = 0; i < argument_count; i++) {
        env.names[i] = entry->parameters[i];
        env.types[i] = arguments[i];
    }
    env.count = argument_count;
    if (ctx->depth >= NYX_TYPE_DEPTH) {
        error_report(ctx->errors, MESSAGE_CHECKER_RECURSIVE_TYPE, entry->name);
        return false;
    }
    ctx->depth++;
    ok = resolve_type_in_env(ctx, alias->right_child, &env, entry->name,
                             out_type);
    ctx->depth--;
    return ok;
}

/* ---- a type written as a name ---- */

/* Built-in type names that cannot stand alone: they take arguments, or are
   made by a function of the context. */
static const struct {
    const char *name;
    const char *message; /* the error, or NULL when `make` builds the type */
    bool (*make)(CheckContext *ctx, size_t *out);
} bare_names[] = {
    {"String", MESSAGE_CHECKER_STRING_ARGS, NULL},
    {"List", MESSAGE_CHECKER_LIST_ARGS, NULL},
    {"Array", MESSAGE_CHECKER_ARRAY_ARGS, NULL},
    {"Record", MESSAGE_CHECKER_RECORD_ARGS, NULL},
    {"Table", MESSAGE_CHECKER_TABLE_ARGS, NULL},
    {"Slice", MESSAGE_CHECKER_SLICE_ARGS, NULL},
    {"Handle", MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, NULL},
    {"Range", MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, NULL},
    {"TextBuilder", NULL, text_builder_type},
    {"Error", NULL, error_type},
};

typedef enum {
    NAME_NEXT,
    NAME_REJECTED,
    NAME_RESOLVED
} NameStep;

static NameStep resolve_builtin_name(CheckContext *ctx, const AstNode *node,
                                     const TypeEnv *env, const char *name,
                                     size_t *out)
{
    NyxType scalar_kind;
    size_t i;

    if (env != NULL && env_lookup(env, name, out)) {
        return NAME_RESOLVED;
    }
    if (builtin_scalar_type_from_name(name, &scalar_kind)) {
        return builtin_type(ctx, scalar_kind, out) ? NAME_RESOLVED : NAME_REJECTED;
    }
    for (i = 0; i < NYX_COUNT_OF(bare_names); i++) {
        if (strcmp(name, bare_names[i].name) != 0) {
            continue;
        }
        if (bare_names[i].make != NULL) {
            return bare_names[i].make(ctx, out) ? NAME_RESOLVED : NAME_REJECTED;
        }
        error_report_at(ctx->errors, node->pos, bare_names[i].message, name);
        return NAME_REJECTED;
    }
    return NAME_NEXT;
}

/* A declared type used by its bare name: an alias to expand or a struct to
   instantiate, neither of which may take arguments here. */
static bool resolve_declared_name(CheckContext *ctx, const AstNode *node,
                                  const char *name, size_t entry_index,
                                  size_t *out)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);

    if (entry->parameter_count != 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, name);
        return false;
    }
    if (entry->type_index != TYPED_NO_NODE) {
        *out = entry->type_index;
        return true;
    }
    if (entry->kind == TYPE_ENTRY_ADT) {
        return expand_adt(ctx, entry_index, NULL, 0, out);
    }
    return entry->kind == TYPE_ENTRY_ALIAS
               ? expand_alias(ctx, entry, NULL, 0, out)
               : instantiate_struct(ctx, entry_index, NULL, 0, out);
}

bool resolve_type_name(CheckContext *ctx, const AstNode *node,
                       const TypeEnv *env, const char *owner, size_t *out_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t entry_index;
    NameStep step;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    step = resolve_builtin_name(ctx, node, env, name, out_index);
    if (step != NAME_NEXT) {
        return step == NAME_RESOLVED;
    }
    entry_index = find_type_index(ctx->types, name);
    if (entry_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        name, owner == NULL ? MESSAGE_FRAGMENT_VALUE : owner);
        return false;
    }
    return resolve_declared_name(ctx, node, name, entry_index, out_index);
}

/* ---- a type written with arguments: Name<...> ---- */

typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    const TypeEnv *env;
    const char *owner;
    const char *name;
    size_t *out;
} Application;

typedef NameStep (*ApplicationRule)(const Application *app);

static NameStep application_step(bool ok)
{
    return ok ? NAME_RESOLVED : NAME_REJECTED;
}

static NameStep application_error(const Application *app, const char *message)
{
    error_report_at(app->ctx->errors, app->node->pos, message, app->name);
    return NAME_REJECTED;
}

static size_t application_child(const Application *app, size_t i)
{
    return *(const size_t *)list_get(&app->ctx->ast->children,
                                     app->node->children_start + i);
}

static NameStep application_needs_name(const Application *app)
{
    if (app->name != NULL) {
        return NAME_NEXT;
    }
    return application_error(app, MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
}

static NameStep application_atomic(const Application *app)
{
    if (strcmp(app->name, "Atomic") != 0) {
        return NAME_NEXT;
    }
    return application_error(app, MESSAGE_CHECKER_ATOMIC_USE);
}

/* Channel<T> and Range<T> take exactly one element type. */
static bool resolve_element(const Application *app, const char *message,
                            size_t *element)
{
    if (app->node->children_count != 1) {
        application_error(app, message);
        return false;
    }
    return resolve_type_in_env(app->ctx, application_child(app, 0), app->env,
                               app->owner, element);
}

static NameStep application_channel(const Application *app)
{
    size_t element;

    if (strcmp(app->name, "Channel") != 0) {
        return NAME_NEXT;
    }
    return application_step(
        resolve_element(app, MESSAGE_CHECKER_CHANNEL_ELEMENT, &element) &&
        check_channel_element(app->ctx, app->node->pos, element) &&
        channel_type(app->ctx, element, app->out));
}

static NameStep application_range(const Application *app)
{
    size_t element;

    if (strcmp(app->name, "Range") != 0) {
        return NAME_NEXT;
    }
    if (!resolve_element(app, MESSAGE_CHECKER_RANGE_ARGUMENTS, &element)) {
        return NAME_REJECTED;
    }
    if (!range_type(app->ctx, element, app->out)) {
        if (!app->ctx->errors->reported) {
            application_error(app, MESSAGE_CHECKER_RANGE_ARGUMENTS);
        }
        return NAME_REJECTED;
    }
    return NAME_RESOLVED;
}

static bool instantiation_in_progress(const CheckContext *ctx)
{
    size_t i;

    for (i = 0; i < ctx->struct_instantiations->length; i++) {
        const StructInstantiation *record =
            list_get(ctx->struct_instantiations, i);

        if (record->in_progress) {
            return true;
        }
    }
    return false;
}

/* The outermost Handle<T> checks that T is a sized, plain value, once no
   struct is half built (a Handle inside its own struct is fine). */
static bool check_outer_handle(const Application *app, size_t element)
{
    CheckContext *ctx = app->ctx;
    bool valid;

    if (ctx->handle_depth != 0 || instantiation_in_progress(ctx)) {
        return true;
    }
    if (!check_handle_payload(ctx, element, &valid)) {
        return false;
    }
    if (!valid) {
        error_report(ctx->errors, MESSAGE_CHECKER_HANDLE_FIXED);
    }
    return valid;
}

static NameStep application_handle(const Application *app)
{
    CheckContext *ctx = app->ctx;
    size_t element;
    bool resolved;

    if (strcmp(app->name, "Handle") != 0) {
        return NAME_NEXT;
    }
    if (app->node->children_count != 1) {
        return application_error(app, MESSAGE_CHECKER_HANDLE_ELEMENT);
    }
    ctx->handle_depth++;
    resolved = resolve_type_in_env(ctx, application_child(app, 0), app->env,
                                   app->owner, &element) &&
               handle_type(ctx, element, app->out);
    ctx->handle_depth--;
    return application_step(resolved && check_outer_handle(app, element));
}

/* Built-in types that take no arguments. */
static const char *const not_generic[] = {
    "Int32", "Bool", "Nil", "Byte", "Store", "String", "Text", "TextBuilder",
    "List", "Array", "Slice", "Record", "Table", "Error",
};

static NameStep application_not_generic(const Application *app)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(not_generic); i++) {
        if (strcmp(app->name, not_generic[i]) == 0) {
            return application_error(app, MESSAGE_CHECKER_TYPE_NOT_GENERIC);
        }
    }
    return NAME_NEXT;
}

/* The arguments of a declared generic type, which may not be resources. */
static bool resolve_arguments(const Application *app, List *arguments)
{
    CheckContext *ctx = app->ctx;
    size_t i;

    for (i = 0; i < app->node->children_count; i++) {
        size_t argument;
        const char *message;

        if (!resolve_type_in_env(ctx, application_child(app, i), app->env,
                                 app->owner, &argument)) {
            return false;
        }
        message = resource_type_message(typed_program_type(ctx->program, argument));
        if (message != NULL) {
            error_report_at(ctx->errors, app->node->pos, message);
            return false;
        }
        checker_ok(ctx->errors, list_push(arguments, &argument),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

static NameStep application_declared(const Application *app)
{
    CheckContext *ctx = app->ctx;
    size_t entry_index = find_type_index(ctx->types, app->name);
    const TypeEntry *entry;
    List arguments;
    bool ok = false;

    if (entry_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, app->node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        app->name,
                        app->owner == NULL ? MESSAGE_FRAGMENT_VALUE : app->owner);
        return NAME_REJECTED;
    }
    list_init(&arguments, sizeof(size_t));
    if (resolve_arguments(app, &arguments)) {
        entry = list_get(ctx->types, entry_index);
        if (entry->parameter_count == 0) {
            application_error(app, MESSAGE_CHECKER_TYPE_NOT_GENERIC);
        } else if (entry->kind == TYPE_ENTRY_ADT) {
            ok = expand_adt(ctx, entry_index, (const size_t *)arguments.items,
                            arguments.length, app->out);
        } else if (entry->kind == TYPE_ENTRY_ALIAS) {
            ok = expand_alias(ctx, entry, (const size_t *)arguments.items,
                              arguments.length, app->out);
        } else {
            ok = instantiate_struct(ctx, entry_index,
                                    (const size_t *)arguments.items,
                                    arguments.length, app->out);
        }
    }
    list_free(&arguments);
    return application_step(ok);
}

static const ApplicationRule application_rules[] = {
    application_needs_name, application_atomic, application_channel,
    application_range, application_handle, application_not_generic,
    application_declared,
};

bool resolve_type_application(CheckContext *ctx, const AstNode *node,
                              const TypeEnv *env, const char *owner,
                              size_t *out_index)
{
    Application app = {ctx, node, env, owner,
                       ast_symbol_text(ctx->ast, node->name_index), out_index};
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(application_rules); i++) {
        NameStep step = application_rules[i](&app);

        if (step != NAME_NEXT) {
            return step == NAME_RESOLVED;
        }
    }
    return false;
}

/* ---- the other forms of type syntax ---- */

typedef bool (*TypeResolver)(CheckContext *ctx, const AstNode *node,
                             const TypeEnv *env, const char *owner, size_t *out);

static bool resolve_left(CheckContext *ctx, const AstNode *node,
                         const TypeEnv *env, const char *owner, size_t *element)
{
    return resolve_type_in_env(ctx, node->left_child, env, owner, element);
}

static bool resolve_fixed_string(CheckContext *ctx, const AstNode *node,
                                 const TypeEnv *env, const char *owner,
                                 size_t *out)
{
    (void)env;
    (void)owner;
    return string_type(ctx, node->integer_value, out);
}

static bool resolve_slice(CheckContext *ctx, const AstNode *node,
                          const TypeEnv *env, const char *owner, size_t *out)
{
    size_t element;

    return resolve_left(ctx, node, env, owner, &element) &&
           slice_type(ctx, element, out);
}

static bool resolve_list(CheckContext *ctx, const AstNode *node,
                         const TypeEnv *env, const char *owner, size_t *out)
{
    size_t element;

    return resolve_left(ctx, node, env, owner, &element) &&
           list_type(ctx, element, false, 0, out);
}

/* A written capacity must fit the configured limit. */
static bool capacity_in_range(CheckContext *ctx, const AstNode *node,
                              const char *message)
{
    if (node->integer_value < 0 ||
        node->integer_value > ctx->program->max_list_capacity) {
        error_report_at(ctx->errors, node->pos, message,
                        ctx->program->max_list_capacity);
        return false;
    }
    return true;
}

static bool resolve_fixed_list(CheckContext *ctx, const AstNode *node,
                               const TypeEnv *env, const char *owner,
                               size_t *out)
{
    size_t element;

    return resolve_left(ctx, node, env, owner, &element) &&
           capacity_in_range(ctx, node, MESSAGE_CHECKER_ARRAY_CAPACITY) &&
           list_type(ctx, element, true, (size_t)node->integer_value, out);
}

static bool resolve_table(CheckContext *ctx, const AstNode *node,
                          const TypeEnv *env, const char *owner, size_t *out)
{
    size_t key_type;
    size_t value_type;

    return resolve_left(ctx, node, env, owner, &key_type) &&
           resolve_type_in_env(ctx, node->right_child, env, owner, &value_type) &&
           map_type(ctx, key_type, value_type, false, 0, out);
}

static bool resolve_record(CheckContext *ctx, const AstNode *node,
                           const TypeEnv *env, const char *owner, size_t *out)
{
    size_t key_type;
    size_t value_type;

    return resolve_left(ctx, node, env, owner, &key_type) &&
           resolve_type_in_env(ctx, node->right_child, env, owner, &value_type) &&
           capacity_in_range(ctx, node, MESSAGE_CHECKER_RECORD_CAPACITY) &&
           map_type(ctx, key_type, value_type, true,
                    (size_t)node->integer_value, out);
}

/* The alternatives of a union, none of them a resource. */
static bool resolve_alternatives(CheckContext *ctx, const AstNode *node,
                                 const TypeEnv *env, const char *owner,
                                 List *alternatives)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t alternative;

        if (!resolve_type_in_env(ctx, child, env, owner, &alternative)) {
            return false;
        }
        if (resource_type_message(typed_program_type(ctx->program, alternative)) !=
            NULL) {
            error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNION_FUNCTION);
            return false;
        }
        checker_ok(ctx->errors, list_push(alternatives, &alternative),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

static bool resolve_union(CheckContext *ctx, const AstNode *node,
                          const TypeEnv *env, const char *owner, size_t *out)
{
    List alternatives;
    const TypedType *result;
    bool ok;

    list_init(&alternatives, sizeof(size_t));
    ok = resolve_alternatives(ctx, node, env, owner, &alternatives) &&
         intern_union(ctx, &alternatives, out);
    list_free(&alternatives);
    result = ok ? typed_program_type(ctx->program, *out) : NULL;
    if (result != NULL && result->kind == NYX_TYPE_NIL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NIL_TYPE);
        return false;
    }
    return ok;
}

static bool resolve_parameter_types(CheckContext *ctx, const AstNode *node,
                                    const TypeEnv *env, const char *owner,
                                    List *parameter_types)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);
        size_t parameter_type;

        if (!resolve_type_in_env(ctx, child, env, owner, &parameter_type)) {
            return false;
        }
        checker_ok(ctx->errors, list_push(parameter_types, &parameter_type),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

/* A function type's result is a value: not a resource, and known. */
/* A function type's result is a value: known, and not a resource. */
static bool check_function_result(CheckContext *ctx, const AstNode *node,
                                  size_t return_type)
{
    const TypedType *result = typed_program_type(ctx->program, return_type);

    if (result != NULL && resource_type_message(result) == NULL) {
        return true;
    }
    error_report_at(ctx->errors, node->pos,
                    result != NULL && result->kind == NYX_TYPE_CHANNEL
                        ? MESSAGE_CHECKER_CHANNEL_USE
                        : MESSAGE_CHECKER_STORE_TYPE);
    return false;
}

static bool resolve_function(CheckContext *ctx, const AstNode *node,
                             const TypeEnv *env, const char *owner, size_t *out)
{
    List parameter_types;
    size_t return_type;
    bool ok;

    list_init(&parameter_types, sizeof(size_t));
    ok = resolve_parameter_types(ctx, node, env, owner, &parameter_types) &&
         (node->right_child == AST_NO_NODE
              ? builtin_type(ctx, NYX_TYPE_NIL, &return_type)
              : resolve_type_in_env(ctx, node->right_child, env, owner,
                                    &return_type)) &&
         check_function_result(ctx, node, return_type) &&
         add_function_type(ctx, &parameter_types, return_type, out);
    list_free(&parameter_types);
    return ok;
}

static bool resolve_name_form(CheckContext *ctx, const AstNode *node,
                              const TypeEnv *env, const char *owner, size_t *out)
{
    return resolve_type_name(ctx, node, env, owner, out);
}

static bool resolve_application_form(CheckContext *ctx, const AstNode *node,
                                     const TypeEnv *env, const char *owner,
                                     size_t *out)
{
    return resolve_type_application(ctx, node, env, owner, out);
}

static bool resolve_case_form(CheckContext *ctx, const AstNode *node,
                              const TypeEnv *env, const char *owner, size_t *out)
{
    return resolve_case_type(ctx, node, env, owner, out);
}

static const struct {
    AstNodeKind kind;
    TypeResolver resolve;
} type_resolvers[] = {
    {AST_TYPE_NAME, resolve_name_form},
    {AST_TYPE_CASE, resolve_case_form},
    {AST_TYPE_FIXED_STRING, resolve_fixed_string},
    {AST_TYPE_SLICE, resolve_slice},
    {AST_TYPE_RECORD, resolve_record},
    {AST_TYPE_TABLE, resolve_table},
    {AST_TYPE_LIST, resolve_list},
    {AST_TYPE_FIXED_LIST, resolve_fixed_list},
    {AST_TYPE_APPLICATION, resolve_application_form},
    {AST_TYPE_UNION, resolve_union},
    {AST_TYPE_FUNCTION, resolve_function},
};

bool resolve_type_in_env(CheckContext *ctx, size_t ast_index, const TypeEnv *env,
                         const char *owner, size_t *out_index)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(type_resolvers); i++) {
        if (type_resolvers[i].kind == node->kind) {
            return type_resolvers[i].resolve(ctx, node, env, owner, out_index);
        }
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}

bool resolve_type_node(CheckContext *ctx, size_t ast_index, const char *owner,
                       size_t *out_index)
{
    return resolve_type_in_env(ctx, ast_index, ctx->env, owner, out_index);
}
