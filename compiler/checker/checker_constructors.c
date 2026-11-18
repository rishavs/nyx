#include "checker_internal.h"

/* Calls that build a struct, a generic struct, or an Error. A struct is built
   from its fields by position or by name; a field left out takes its default.
   For a generic struct the type arguments are first inferred from the values
   given, then the call is checked as a call of the instance. */

typedef struct {
    size_t expression; /* the syntax of the value for a field */
    SourcePos pos;     /* where a mismatch is reported */
} FieldSource;

static size_t argument_at(const CheckContext *ctx, const AstNode *node, size_t i)
{
    return *(const size_t *)list_get(&ctx->ast->children,
                                     node->children_start + i);
}

/* Whether the arguments are given by name; judged by the first. */
static bool constructor_is_named(CheckContext *ctx, const AstNode *node,
                                 bool *named)
{
    const AstNode *first;

    *named = false;
    if (node->children_count == 0) {
        return true;
    }
    first = ast_get_node(ctx->ast, argument_at(ctx, node, 0));
    if (first == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE,
                     argument_at(ctx, node, 0));
        return false;
    }
    *named = first->kind == AST_NAMED_ARG;
    return true;
}

/* Named arguments must each name a field, and none twice. An unknown name is
   reported before a missing one, so a typo reads as "unknown field". */
static bool check_named_arguments(CheckContext *ctx, const AstNode *node,
                                  const AstNode *declaration)
{
    size_t i;
    size_t j;

    for (i = 0; i < node->children_count; i++) {
        const AstNode *argument = ast_get_node(ctx->ast, argument_at(ctx, node, i));
        const char *name;

        if (argument == NULL || argument->kind != AST_NAMED_ARG) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        name = ast_symbol_text(ctx->ast, argument->name_index);
        if (template_field_index(ctx, declaration, name) == TYPED_NO_NODE) {
            error_report_at(ctx->errors, argument->pos,
                            MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD, name);
            return false;
        }
        for (j = 0; j < i; j++) {
            const char *other = ast_symbol_text(
                ctx->ast,
                ast_get_node(ctx->ast, argument_at(ctx, node, j))->name_index);

            if (other != NULL && strcmp(other, name) == 0) {
                error_report_at(ctx->errors, argument->pos,
                                MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG, name);
                return false;
            }
        }
    }
    return true;
}

/* The named argument for a field, if one was given. */
static const AstNode *named_argument(const CheckContext *ctx, const AstNode *node,
                                     const char *field_name)
{
    size_t j;

    for (j = 0; j < node->children_count; j++) {
        const AstNode *argument = ast_get_node(ctx->ast, argument_at(ctx, node, j));
        const char *name = ast_symbol_text(ctx->ast, argument->name_index);

        if (name != NULL && field_name != NULL && strcmp(name, field_name) == 0) {
            return argument;
        }
    }
    return NULL;
}

/* A field with no argument and no default. */
static void report_missing_field(CheckContext *ctx, const AstNode *node,
                                 const AstNode *declaration,
                                 const char *struct_name, const char *field_name,
                                 bool named)
{
    error_report_at(ctx->errors, node->pos,
                    named ? MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG
                          : MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                    named ? field_name : struct_name,
                    declaration->children_count, node->children_count);
}

/* Where the value of field `i` comes from: the argument, or the field's own
   default when no argument was given. */
static bool field_source(CheckContext *ctx, const AstNode *node,
                         const AstNode *declaration, const char *struct_name,
                         size_t i, bool named, FieldSource *source)
{
    const AstNode *field = template_field(ctx, declaration, i, NULL);
    const char *field_name =
        field == NULL ? NULL : ast_symbol_text(ctx->ast, field->name_index);
    const AstNode *given = named ? named_argument(ctx, node, field_name) : NULL;
    bool has_default = field != NULL && field->right_child != AST_NO_NODE;

    source->pos = given == NULL ? node->pos : given->pos;
    if (given != NULL) {
        source->expression = given->left_child;
    } else if (!named && i < node->children_count) {
        source->expression = argument_at(ctx, node, i);
    } else if (has_default) {
        source->expression = field->right_child;
    } else {
        report_missing_field(ctx, node, declaration, struct_name, field_name,
                             named);
        return false;
    }
    return true;
}

/* The arguments themselves must be well formed before any field is read. */
static bool check_argument_shape(CheckContext *ctx, const AstNode *node,
                                 const AstNode *declaration,
                                 const char *struct_name, bool named)
{
    if (named) {
        return check_named_arguments(ctx, node, declaration);
    }
    if (node->children_count > declaration->children_count) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT, struct_name,
                        declaration->children_count, node->children_count);
        return false;
    }
    return true;
}

/* ---- a struct ---- */

typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    const TypeEntry *entry;
    const AstNode *declaration;
    size_t decl_index; /* the typed declaration, kept as an index because
                          checking an argument can move the node array */
    size_t field_count;
    bool named;
    List values;
} Constructor;

static size_t field_type_of(const Constructor *k, size_t i)
{
    return struct_field_type(k->ctx->program,
                             typed_program_node(k->ctx->program, k->decl_index),
                             i);
}

/* Check the value for field `i`, coerce it to the field's type and keep it. */
static bool constructor_field(Constructor *k, size_t i)
{
    CheckContext *ctx = k->ctx;
    FieldSource source;
    size_t value;
    size_t coerced;

    if (!field_source(ctx, k->node, k->declaration, k->entry->name, i, k->named,
                      &source) ||
        !check_expression_expected(ctx, source.expression, field_type_of(k, i),
                                   &value) ||
        !constructor_value_fits(ctx, value, k->entry, i, i + 1, source.pos,
                                &coerced)) {
        return false;
    }
    checker_ok(ctx->errors, list_push(&k->values, &coerced),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

/* The field a named argument is for, as an index into the declaration. */
static size_t constructor_field_index(const Constructor *k, const char *name)
{
    size_t j;

    for (j = 0; j < k->field_count; j++) {
        const char *field = struct_field_name(
            k->ctx->program, typed_program_node(k->ctx->program, k->decl_index), j);

        if (field != NULL && name != NULL && strcmp(field, name) == 0) {
            return j;
        }
    }
    return k->field_count;
}

/* With named arguments the values are evaluated in the order written, then the
   defaults in field order; this is that order, as field positions. */
static bool constructor_order(const Constructor *k, List *order)
{
    CheckContext *ctx = k->ctx;
    size_t i;
    size_t j;

    for (i = 0; i < k->node->children_count; i++) {
        const AstNode *argument =
            ast_get_node(ctx->ast, argument_at(ctx, k->node, i));
        size_t field = constructor_field_index(
            k, ast_symbol_text(ctx->ast, argument->name_index));

        checker_ok(ctx->errors, list_push(order, &field),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    for (i = 0; i < k->field_count; i++) {
        bool supplied = false;

        for (j = 0; j < order->length; j++) {
            supplied = supplied || *(const size_t *)list_get(order, j) == i;
        }
        if (!supplied) {
            checker_ok(ctx->errors, list_push(order, &i),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    return !ctx->errors->fatal;
}

static bool emit_constructor(Constructor *k, size_t ast_index)
{
    CheckContext *ctx = k->ctx;
    TypedNode typed = new_node(TYPED_STRUCT_CONSTRUCTOR, k->entry->type_index,
                               k->node->pos, TYPED_NO_NODE, TYPED_NO_NODE);
    List order;
    bool ok;

    /* Nested constructors in the arguments have appended their own runs
       already, so this constructor's values become one contiguous run. */
    typed.children_start = push_typed_children(ctx, &k->values);
    typed.children_count = k->field_count;
    if (!k->named) {
        return emit_sequenced_expression(ctx, &typed, ast_index, NULL);
    }
    list_init(&order, sizeof(size_t));
    ok = constructor_order(k, &order) &&
         emit_sequenced_expression(ctx, &typed, ast_index, &order);
    list_free(&order);
    return ok;
}

bool check_constructor(CheckContext *ctx, const AstNode *node,
                       const TypeEntry *entry, size_t ast_index)
{
    Constructor k = {0};
    const TypedNode *type_decl;
    size_t i;
    bool ok;

    k.ctx = ctx;
    k.node = node;
    k.entry = entry;
    k.decl_index = struct_declaration_node(ctx, entry);
    k.declaration = ast_get_node(ctx->ast, entry->ast_index);
    type_decl = typed_program_node(ctx->program, k.decl_index);
    if (type_decl == NULL || type_decl->kind != TYPED_TYPE_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    k.field_count = type_decl->children_count;
    if (!constructor_is_named(ctx, node, &k.named) ||
        !check_argument_shape(ctx, node, k.declaration, entry->name, k.named)) {
        return false;
    }
    list_init(&k.values, sizeof(size_t));
    ok = true;
    for (i = 0; ok && i < k.field_count; i++) {
        ok = constructor_field(&k, i);
    }
    ok = ok && emit_constructor(&k, ast_index);
    list_free(&k.values);
    return ok;
}

/* ---- a generic struct ---- */

/* The struct type the context expects, when it is an instance of this
   generic struct: the call is then a call of that instance. */
static bool expected_instance(const CheckContext *ctx, const TypeEntry *entry,
                              size_t ast_index)
{
    const TypedType *expected =
        ctx->expected_ast == ast_index
            ? typed_program_type(ctx->program, ctx->expected_type)
            : NULL;
    const char *source_name =
        expected == NULL || expected->source_name_index == TYPED_NO_NODE
            ? NULL
            : typed_program_symbol_text(ctx->program,
                                        expected->source_name_index);

    return expected != NULL && expected->kind == NYX_TYPE_STRUCT &&
           source_name != NULL && strcmp(source_name, entry->name) == 0 &&
           expected->child_count == entry->parameter_count;
}

/* Check the value for field `i` and let it fix the type parameters its field
   type mentions. */
static bool infer_from_field(CheckContext *ctx, const AstNode *node,
                             const AstNode *declaration, const TypeEntry *entry,
                             size_t i, bool named, InferEnv *infer)
{
    const AstNode *field = template_field(ctx, declaration, i, NULL);
    const TypedNode *value_node;
    FieldSource source;
    size_t value;

    if (!field_source(ctx, node, declaration, entry->name, i, named, &source) ||
        !check_expression(ctx, source.expression, &value)) {
        return false;
    }
    value_node = typed_program_node(ctx->program, value);
    if (value_node == NULL ||
        (ast_type_has_unbound(ctx, field->left_child, infer, NYX_TYPE_DEPTH) &&
         (!check_default_numeric(ctx, value) ||
          !infer_ast_type(ctx, field->left_child, value_node->type_index, infer,
                          entry->name)))) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_CANNOT_INFER_TYPE,
                        infer_first_unbound(infer), entry->name);
        return false;
    }
    return true;
}

/* Infer the type arguments from the values given, then instantiate the struct
   with them. */
static bool instantiate_from_values(CheckContext *ctx, const AstNode *node,
                                    const AstNode *declaration,
                                    const TypeEntry *entry, size_t entry_index,
                                    size_t *type_index)
{
    InferEnv infer = {0};
    TypeEnv env = {0};
    size_t arguments[NYX_MAX_TYPE_PARAMETERS];
    size_t i;
    bool named;

    for (i = 0; i < entry->parameter_count; i++) {
        infer.names[i] = entry->parameters[i];
        infer.types[i] = TYPED_NO_NODE;
    }
    infer.count = entry->parameter_count;
    if (!constructor_is_named(ctx, node, &named) ||
        !check_argument_shape(ctx, node, declaration, entry->name, named)) {
        return false;
    }
    for (i = 0; i < declaration->children_count; i++) {
        if (!infer_from_field(ctx, node, declaration, entry, i, named, &infer)) {
            return false;
        }
    }
    if (!infer_finish(ctx, &infer, &env, entry->name)) {
        return false;
    }
    for (i = 0; i < env.count; i++) {
        arguments[i] = env.types[i];
    }
    return instantiate_struct(ctx, entry_index, arguments, env.count, type_index);
}

bool check_generic_constructor(CheckContext *ctx, const AstNode *node,
                               size_t entry_index, size_t ast_index)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, entry->ast_index);
    TypeEntry concrete = *entry;
    size_t type_index = ctx->expected_type;

    if (declaration == NULL || declaration->kind != AST_TYPE_DECLARATION) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (!expected_instance(ctx, entry, ast_index) &&
        !instantiate_from_values(ctx, node, declaration, entry, entry_index,
                                 &type_index)) {
        return false;
    }
    concrete.type_index = type_index;
    return check_constructor(ctx, node, &concrete, ast_index);
}

/* ---- Error ---- */

/* The positional argument `i`, or AST_NO_NODE when it is missing or named. */
static size_t positional_argument(const CheckContext *ctx, const AstNode *node,
                                  size_t i)
{
    size_t child = argument_at(ctx, node, i);
    const AstNode *argument = ast_get_node(ctx->ast, child);

    return argument == NULL || argument->kind == AST_NAMED_ARG ? AST_NO_NODE
                                                               : child;
}

/* Error.throw(header, description): the file name is the source being
   compiled, stored with the node. */
/* The types of the three string fields of an Error. */
static bool error_string_types(CheckContext *ctx, size_t error, size_t *types)
{
    return error_field_type(ctx, error, 0, &types[0]) &&
           error_field_type(ctx, error, 1, &types[1]) &&
           error_field_type(ctx, error, 2, &types[2]);
}

/* The types of the six arguments of Error(...): three strings, three
   integers. */
static bool error_argument_types(CheckContext *ctx, size_t error, size_t *types)
{
    size_t int32_type;
    size_t i;

    if (!error_string_types(ctx, error, types) ||
        !builtin_type(ctx, NYX_TYPE_INT32, &int32_type)) {
        return false;
    }
    for (i = 3; i < 6; i++) {
        types[i] = int32_type;
    }
    return true;
}

/* The two arguments of Error.throw, coerced to the header and description
   types. */
static bool throw_arguments(CheckContext *ctx, const AstNode *node,
                            const size_t *types, size_t *values)
{
    size_t i;

    if (node->children_count != 2) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ERROR_THROW_ARGS);
        return false;
    }
    for (i = 0; i < 2; i++) {
        size_t child = positional_argument(ctx, node, i);
        size_t value;

        if (child == AST_NO_NODE || !check_expression(ctx, child, &value) ||
            !inject_into(ctx, value, types[i], &values[i])) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_ERROR_THROW_ARGS);
            }
            return false;
        }
    }
    return true;
}

/* The name of the source being compiled, cut to the Error's filename field,
   stored in the typed program's bytes. Returns its length. */
static size_t push_source_name(CheckContext *ctx, size_t filename_type)
{
    const TypedType *record = typed_program_type(ctx->program, filename_type);
    const char *filename = ctx->errors->source_name == NULL
                               ? MESSAGE_FRAGMENT_DEFAULT_SOURCE_NAME
                               : ctx->errors->source_name;
    size_t length = strlen(filename);
    size_t i;

    if (record != NULL && length > record->fixed_length) {
        length = record->fixed_length;
    }
    for (i = 0; i < length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_byte(ctx->program, (unsigned char)filename[i]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return length;
}

/* Error.throw(header, description): the file name is the source being
   compiled, stored with the node. */
bool check_error_throw(CheckContext *ctx, const AstNode *node, size_t ast_index)
{
    size_t error;
    size_t types[6];
    size_t values[2];
    List arguments;
    TypedNode typed;
    bool ok;

    ok = error_type(ctx, &error) && error_string_types(ctx, error, types) &&
         throw_arguments(ctx, node, types, values);
    if (!ok) {
        return false;
    }
    typed = new_node(TYPED_ERROR_THROW, error, node->pos, TYPED_NO_NODE,
                     TYPED_NO_NODE);
    typed.auxiliary_type = types[2];
    typed.store_id = TYPED_NO_NODE;
    typed.string_offset = ctx->program->bytes.length;
    typed.string_length = push_source_name(ctx, types[2]);
    list_init(&arguments, sizeof(size_t));
    checker_ok(ctx->errors, list_push(&arguments, &values[0]),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors, list_push(&arguments, &values[1]),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    typed.children_start = push_typed_children(ctx, &arguments);
    typed.children_count = 2;
    list_free(&arguments);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* Argument `i` of Error(...): positional, and coerced to its field's type. */
static bool error_argument(CheckContext *ctx, const AstNode *node, size_t i,
                           size_t expected, size_t *coerced)
{
    size_t child = positional_argument(ctx, node, i);
    size_t value;

    if (child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ERROR_ARGS);
        return false;
    }
    if (!check_expression(ctx, child, &value)) {
        return false;
    }
    if (!inject_into(ctx, value, expected, coerced)) {
        error_report_at(ctx->errors, ast_get_node(ctx->ast, child)->pos,
                        MESSAGE_CHECKER_ERROR_ARG_TYPE, i + 1);
        return false;
    }
    return true;
}

bool check_error_constructor(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    size_t error;
    size_t expected[6];
    List values;
    size_t i;
    bool ok = error_type(ctx, &error) && error_argument_types(ctx, error, expected);
    TypedNode typed;

    if (!ok) {
        return false;
    }
    if (node->children_count != 6) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ERROR_ARGS);
        return false;
    }
    list_init(&values, sizeof(size_t));
    for (i = 0; ok && i < 6; i++) {
        size_t coerced;

        ok = error_argument(ctx, node, i, expected[i], &coerced);
        if (ok) {
            checker_ok(ctx->errors, list_push(&values, &coerced),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    typed = new_node(TYPED_ERROR_CONSTRUCT, error, node->pos, TYPED_NO_NODE,
                     TYPED_NO_NODE);
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ok ? push_typed_children(ctx, &values) : 0;
    typed.children_count = values.length;
    list_free(&values);
    return ok && emit_node(ctx, &typed, ast_index, &(size_t){0});
}
