#include "checker_internal.h"

/* Sum types declared with cases. `type Shape is | Circle is radius: Int32 end |
   Empty end` is checked as if it were written as a struct per case and a union
   of them: each case is a struct entry (owner `Shape`, name `Circle`), and
   `Shape` itself is the union of the case structs, built when the name is used.
   Everything a union already does, that is the tag, `typeof` tests, `match`,
   narrowing, exhaustiveness and printing, therefore holds for a sum type
   without a second implementation; this file only adds the names. A sum type
   may be generic. Its cases share its type parameters, so `Option<Int32>` is
   the union of `Some<Int32>` and `None<Int32>`. */

bool is_sum_type_name(const CheckContext *ctx, const char *name)
{
    const TypeEntry *entry = find_type(ctx->types, name);

    return entry != NULL && entry->kind == TYPE_ENTRY_ADT;
}

static const AstNode *case_node(const CheckContext *ctx,
                                const AstNode *declaration, size_t i,
                                size_t *out_index)
{
    *out_index = *(const size_t *)list_get(&ctx->ast->children,
                                           declaration->children_start + i);
    return ast_get_node(ctx->ast, *out_index);
}

static bool case_is_repeated(const CheckContext *ctx, const AstNode *declaration,
                             size_t i, const char *name)
{
    size_t j;

    for (j = 0; j < i; j++) {
        size_t index;
        const char *earlier = ast_symbol_text(
            ctx->ast, case_node(ctx, declaration, j, &index)->name_index);

        if (strcmp(earlier, name) == 0) {
            return true;
        }
    }
    return false;
}

/* The entry of one case: a struct named by the case and owned by the sum type,
   with the sum type's parameters. */
static bool declare_case(CheckContext *ctx, const AstNode *declaration, size_t i,
                         size_t adt_entry)
{
    const TypeEntry *adt = list_get(ctx->types, adt_entry);
    TypeEntry entry = {0};
    size_t index;
    const AstNode *node = case_node(ctx, declaration, i, &index);
    const char *name = ast_symbol_text(ctx->ast, node->name_index);

    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (case_is_repeated(ctx, declaration, i, name)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_CASE,
                        name, adt->name);
        return false;
    }
    memcpy(entry.name, name, strlen(name) + 1);
    memcpy(entry.owner, adt->name, strlen(adt->name) + 1);
    entry.kind = TYPE_ENTRY_STRUCT;
    entry.ast_index = index;
    entry.type_index = TYPED_NO_NODE;
    entry.parameter_count = adt->parameter_count;
    memcpy(entry.parameters, adt->parameters, sizeof(entry.parameters));
    return check_duplicate_fields(ctx, node, name) &&
           (entry.parameter_count == 0 ||
            check_expanding_fields(ctx, node, name, &entry)) &&
           checker_ok(ctx->errors, list_push(ctx->types, &entry),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

bool declare_cases(CheckContext *ctx, const AstNode *declaration,
                   size_t adt_entry)
{
    size_t i;

    for (i = 0; i < declaration->children_count; i++) {
        if (!declare_case(ctx, declaration, i, adt_entry)) {
            return false;
        }
    }
    return true;
}

/* The struct of case `i` for these type arguments, added to `alternatives`. A
   sum type with no parameters remembers the struct on the case. */
static bool instantiate_case(CheckContext *ctx, const TypeEntry *adt,
                             const AstNode *declaration, size_t i,
                             const size_t *arguments, size_t argument_count,
                             List *alternatives)
{
    size_t index;
    size_t case_entry = find_case_index(
        ctx->types, adt->name,
        ast_symbol_text(ctx->ast,
                        case_node(ctx, declaration, i, &index)->name_index));
    size_t type;

    if (!instantiate_struct(ctx, case_entry, arguments, argument_count, &type)) {
        return false;
    }
    if (adt->parameter_count == 0) {
        ((TypeEntry *)list_get(ctx->types, case_entry))->type_index = type;
    }
    return checker_ok(ctx->errors, list_push(alternatives, &type),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* The union of the case structs for these type arguments. For a sum type with
   no parameters it is built once. */
bool expand_adt(CheckContext *ctx, size_t entry_index, const size_t *arguments,
                size_t argument_count, size_t *out_type)
{
    TypeEntry adt = *(const TypeEntry *)list_get(ctx->types, entry_index);
    const AstNode *declaration = ast_get_node(ctx->ast, adt.ast_index);
    List alternatives;
    size_t i;
    bool ok = true;

    if (argument_count != adt.parameter_count) {
        error_report(ctx->errors, MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT, adt.name,
                     adt.parameter_count, argument_count);
        return false;
    }
    list_init(&alternatives, sizeof(size_t));
    for (i = 0; ok && i < declaration->children_count; i++) {
        ok = instantiate_case(ctx, &adt, declaration, i, arguments,
                              argument_count, &alternatives);
    }
    ok = ok && intern_union(ctx, &alternatives, out_type);
    list_free(&alternatives);
    if (ok && adt.parameter_count == 0) {
        ((TypeEntry *)list_get(ctx->types, entry_index))->type_index = *out_type;
    }
    return ok;
}

/* The type arguments written after a sum type's name. */
static bool written_arguments(CheckContext *ctx, const AstNode *node,
                              const TypeEnv *env, const char *owner,
                              size_t *arguments)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        if (!resolve_type_in_env(
                ctx,
                *(const size_t *)list_get(&ctx->ast->children,
                                          node->children_start + i),
                env, owner, &arguments[i])) {
            return false;
        }
    }
    return true;
}

/* The entry of `owner::name`, or the diagnostic for why there is none. */
static bool find_case(CheckContext *ctx, const AstNode *node, const char *owner,
                      const char *name, size_t *out_entry)
{
    size_t adt_index = find_type_index(ctx->types, owner);

    if (adt_index == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_TYPE,
                        owner, MESSAGE_FRAGMENT_VALUE);
        return false;
    }
    if (((const TypeEntry *)list_get(ctx->types, adt_index))->kind !=
        TYPE_ENTRY_ADT) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NOT_A_SUM_TYPE,
                        owner);
        return false;
    }
    *out_entry = find_case_index(ctx->types, owner, name);
    if (*out_entry == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_UNKNOWN_CASE,
                        owner, name);
        return false;
    }
    return true;
}

/* `Owner::Case` written as a type: the struct of that case. */
bool resolve_case_type(CheckContext *ctx, const AstNode *node,
                       const TypeEnv *env, const char *owner, size_t *out_type)
{
    const char *sum = ast_symbol_text(ctx->ast, node->name_index);
    const char *name = ast_symbol_text(ctx->ast, node->second_name_index);
    size_t arguments[NYX_MAX_TYPE_PARAMETERS];
    size_t entry_index;
    const TypeEntry *entry;

    if (node->children_count > NYX_MAX_TYPE_PARAMETERS ||
        !find_case(ctx, node, sum, name, &entry_index) ||
        !written_arguments(ctx, node, env, owner, arguments)) {
        return false;
    }
    entry = list_get(ctx->types, entry_index);
    if (node->children_count == 0 && entry->parameter_count != 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, sum);
        return false;
    }
    return instantiate_struct(ctx, entry_index, arguments, node->children_count,
                              out_type);
}

/* The type of an Error's `kind`: the sum type the prelude declares. */
bool error_kind_type(CheckContext *ctx, size_t *out_type)
{
    size_t entry_index = find_type_index(ctx->types, "ErrorKind");

    if (entry_index == TYPED_NO_NODE) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNKNOWN_TYPE, "ErrorKind",
                     MESSAGE_FRAGMENT_VALUE);
        return false;
    }
    return expand_adt(ctx, entry_index, NULL, 0, out_type);
}

/* ---- building a case ---- */

/* The type arguments of `alternative` when it is a struct of the case `name`
   with the right number of arguments. */
static bool alternative_arguments(const CheckContext *ctx,
                                  const TypedType *alternative,
                                  const char *name, size_t parameter_count,
                                  size_t *arguments)
{
    const char *source =
        alternative->source_name_index == TYPED_NO_NODE
            ? NULL
            : typed_program_symbol_text(ctx->program,
                                        alternative->source_name_index);
    size_t i;

    if (source == NULL || strcmp(source, name) != 0 ||
        alternative->child_count != parameter_count) {
        return false;
    }
    for (i = 0; i < parameter_count; i++) {
        arguments[i] = *(const size_t *)list_get(&ctx->program->type_children,
                                                 alternative->first_child + i);
    }
    return true;
}

/* The arguments of a generic case, when the context already says which
   instance of the sum type is wanted: the expected type holds a struct of this
   case. */
static bool arguments_from_expected(const CheckContext *ctx, size_t ast_index,
                                    const char *name, size_t parameter_count,
                                    size_t *arguments)
{
    const TypedType *expected =
        ctx->expected_ast == ast_index
            ? typed_program_type(ctx->program, ctx->expected_type)
            : NULL;
    size_t i;

    for (i = 0; expected != NULL && expected->kind == NYX_TYPE_UNION &&
                i < expected->child_count;
         i++) {
        const TypedType *alternative = typed_program_type(
            ctx->program, *(const size_t *)list_get(
                              &ctx->program->type_children,
                              expected->first_child + i));

        if (alternative_arguments(ctx, alternative, name, parameter_count,
                                  arguments)) {
            return true;
        }
    }
    return false;
}

/* The struct type a call of a case builds. For a generic sum type the type
   arguments come from the call (`Option<Int32>::Some`), else from the type the
   context expects, else from the values given. */
static bool case_struct_type(CheckContext *ctx, const AstNode *node,
                             size_t ast_index, size_t entry_index,
                             size_t *out_type)
{
    const TypeEntry *entry = list_get(ctx->types, entry_index);
    size_t arguments[NYX_MAX_TYPE_PARAMETERS];
    const AstNode *written = ast_get_node(ctx->ast, node->owner_type_index);

    if (entry->parameter_count == 0) {
        return instantiate_struct(ctx, entry_index, NULL, 0, out_type);
    }
    if (written != NULL && written->kind == AST_TYPE_APPLICATION) {
        return written->children_count <= NYX_MAX_TYPE_PARAMETERS &&
               written_arguments(ctx, written, ctx->env, NULL, arguments) &&
               instantiate_struct(ctx, entry_index, arguments,
                                  written->children_count, out_type);
    }
    if (arguments_from_expected(ctx, ast_index, entry->name,
                                entry->parameter_count, arguments)) {
        return instantiate_struct(ctx, entry_index, arguments,
                                  entry->parameter_count, out_type);
    }
    return instantiate_from_values(ctx, node,
                                   ast_get_node(ctx->ast, entry->ast_index),
                                   entry, entry_index, out_type);
}

/* The sum type value that holds the case value just checked for `ast_index`.
   When the context asks for the case itself, the struct is the result. */
static bool wrap_in_sum_type(CheckContext *ctx, size_t adt_entry,
                             size_t struct_type, size_t ast_index)
{
    size_t arguments[NYX_MAX_TYPE_PARAMETERS];
    size_t count = typed_program_type(ctx->program, struct_type)->child_count;
    size_t first = typed_program_type(ctx->program, struct_type)->first_child;
    size_t value = *mapping_slot(ctx->mapping, ast_index);
    size_t union_type;
    size_t tag;
    size_t injected;
    size_t i;

    if (ctx->expected_ast == ast_index && ctx->expected_type == struct_type) {
        return true;
    }
    for (i = 0; i < count; i++) {
        arguments[i] = *(const size_t *)list_get(&ctx->program->type_children,
                                                 first + i);
    }
    if (!expand_adt(ctx, adt_entry, arguments, count, &union_type)) {
        return false;
    }
    if (union_type == struct_type) {
        return true;
    }
    if (!union_has_alternative(ctx->program,
                               typed_program_type(ctx->program, union_type),
                               struct_type, &tag) ||
        !push_injection(ctx, value, union_type, tag, &injected)) {
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = injected;
    return true;
}

bool check_case_call(CheckContext *ctx, const AstNode *node, const char *owner,
                     size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t entry_index;
    TypeEntry concrete;
    size_t struct_type;

    if (!find_case(ctx, node, owner, name, &entry_index) ||
        !case_struct_type(ctx, node, ast_index, entry_index, &struct_type)) {
        return false;
    }
    concrete = *(const TypeEntry *)list_get(ctx->types, entry_index);
    concrete.type_index = struct_type;
    return check_constructor(ctx, node, &concrete, ast_index) &&
           wrap_in_sum_type(ctx, find_type_index(ctx->types, owner), struct_type,
                            ast_index);
}
