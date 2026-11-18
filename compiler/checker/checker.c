#include "checker_internal.h"

static bool validate_source_type_depth(CheckContext *ctx, size_t index, bool union_alternative, size_t depth);

/* Which operands of a type syntax node hold another type. */
static const struct {
    AstNodeKind kind;
    bool left;
    bool right;
} type_operands[] = {
    {AST_TYPE_LIST, true, false},   {AST_TYPE_FIXED_LIST, true, false},
    {AST_TYPE_SLICE, true, false},  {AST_TYPE_RECORD, true, true},
    {AST_TYPE_TABLE, true, true},   {AST_TYPE_FUNCTION, false, true},
};

static bool is_nil_name(const CheckContext *ctx, size_t index)
{
    const AstNode *node = ast_get_node(ctx->ast, index);
    const char *name = node != NULL && node->kind == AST_TYPE_NAME
                           ? ast_symbol_text(ctx->ast, node->name_index)
                           : NULL;

    return name != NULL && strcmp(name, "Nil") == 0;
}

/* The children of a union, a type application or a function type. A union of
   nothing but Nil has no value to hold. */
static bool validate_alternatives(CheckContext *ctx, const AstNode *node,
                                  size_t depth)
{
    bool is_union = node->kind == AST_TYPE_UNION;
    bool only_nil = is_union;
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->ast->children,
                                                 node->children_start + i);

        only_nil = only_nil && is_nil_name(ctx, child);
        if (!validate_source_type_depth(ctx, child, is_union, depth - 1)) {
            return false;
        }
    }
    if (only_nil) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NIL_TYPE);
    }
    return !only_nil;
}

static bool validate_operands(CheckContext *ctx, const AstNode *node,
                              size_t depth)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(type_operands); i++) {
        if (type_operands[i].kind != node->kind) {
            continue;
        }
        return (!type_operands[i].left ||
                validate_source_type_depth(ctx, node->left_child, false,
                                           depth - 1)) &&
               (!type_operands[i].right ||
                validate_source_type_depth(ctx, node->right_child, false,
                                           depth - 1));
    }
    return true;
}

/* A bare Nil is no type to hold a value, except as a union alternative. */
static bool validate_name(CheckContext *ctx, const AstNode *node, size_t index,
                          bool union_alternative)
{
    if (!union_alternative && is_nil_name(ctx, index)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NIL_TYPE);
        return false;
    }
    return true;
}

static bool validate_source_type_depth(CheckContext *ctx, size_t index,
                                       bool union_alternative, size_t depth)
{
    const AstNode *node = ast_get_node(ctx->ast, index);
    bool alternatives;

    if (node == NULL) {
        return true;
    }
    if (depth == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_PARSER_TREE_DEPTH_LIMIT, 256);
        return false;
    }
    if (node->kind == AST_TYPE_NAME) {
        return validate_name(ctx, node, index, union_alternative);
    }
    alternatives = node->kind == AST_TYPE_UNION ||
                   node->kind == AST_TYPE_APPLICATION ||
                   node->kind == AST_TYPE_FUNCTION;
    return (!alternatives || validate_alternatives(ctx, node, depth)) &&
           validate_operands(ctx, node, depth);
}

static bool validate_source_type(CheckContext *ctx, size_t index,
                                 bool union_alternative)
{
    return validate_source_type_depth(ctx, index, union_alternative, 256);
}

static bool mark_invalid_root(CheckContext *ctx, List *invalid_roots, SourcePos pos)
{
    size_t low = 0;
    size_t high = ctx->ast->roots.length;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        size_t index = *(const size_t *)list_get(&ctx->ast->roots, mid);
        const AstNode *root = ast_get_node(ctx->ast, index);
        if (root == NULL) return false;
        if (root->pos.offset <= pos.offset) low = mid + 1;
        else high = mid;
    }
    if (low == 0) return false;
    *(bool *)list_get(invalid_roots, low - 1) = true;
    return true;
}

/* Where each kind of node keeps a source type. A static call keeps its owner
   type; `is` and match patterns name union alternatives, where Nil is allowed. */
static const struct {
    AstNodeKind kind;
    size_t offset;
    bool static_only;
    bool union_alternative;
} source_types[] = {
    {AST_FIELD, offsetof(AstNode, left_child), false, false},
    {AST_PARAMETER, offsetof(AstNode, left_child), false, false},
    {AST_DECLARATION, offsetof(AstNode, right_child), false, false},
    {AST_FUNCTION_DECLARATION, offsetof(AstNode, right_child), false, false},
    {AST_TYPE_ALIAS, offsetof(AstNode, right_child), false, false},
    {AST_MEMBER_CALL, offsetof(AstNode, right_child), false, false},
    {AST_CALL, offsetof(AstNode, owner_type_index), true, false},
    {AST_IS, offsetof(AstNode, right_child), false, true},
};

static size_t source_type_index(const AstNode *node, bool *union_alternative)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(source_types); i++) {
        if (source_types[i].kind != node->kind ||
            (source_types[i].static_only && !node->is_static)) {
            continue;
        }
        *union_alternative = source_types[i].union_alternative;
        return *(const size_t *)((const char *)node + source_types[i].offset);
    }
    return AST_NO_NODE;
}

/* Validate one source type; a bad one marks the root that holds it. Returns
   false when that root cannot be found. */
static bool check_source_type(CheckContext *ctx, List *invalid_roots,
                              const AstNode *node, size_t index,
                              bool union_alternative, bool *valid)
{
    if (index == AST_NO_NODE ||
        validate_source_type(ctx, index, union_alternative)) {
        return true;
    }
    *valid = false;
    return mark_invalid_root(ctx, invalid_roots, node->pos);
}

/* A match's patterns sit at the even children, each followed by its arm. */
static bool check_match_patterns(CheckContext *ctx, List *invalid_roots,
                                 const AstNode *node, bool *valid)
{
    size_t j;

    for (j = 0; j < node->children_count; j += 2) {
        size_t pattern = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + j);

        if (!check_source_type(ctx, invalid_roots, node, pattern, true, valid)) {
            return false;
        }
    }
    return true;
}

static bool validate_source_types(CheckContext *ctx, List *invalid_roots)
{
    bool valid = true;
    size_t i;

    for (i = 0; i < ctx->ast->nodes.length; i++) {
        const AstNode *node = ast_get_node(ctx->ast, i);
        bool union_alternative = false;
        size_t index = source_type_index(node, &union_alternative);
        bool matches = (node->kind == AST_MATCH ||
                        node->kind == AST_MATCH_EXPRESSION) &&
                       node->integer_value != 0;

        if (!check_source_type(ctx, invalid_roots, node, index,
                               union_alternative, &valid) ||
            (matches && !check_match_patterns(ctx, invalid_roots, node, &valid))) {
            return false;
        }
    }
    return valid;
}


Checkpoint checker_checkpoint(const CheckContext *ctx)
{
    return (Checkpoint){ctx->types->length, ctx->functions->length,
                        ctx->struct_instantiations->length,
                        ctx->function_instantiations->length,
                        ctx->locals->length, ctx->globals->length,
                        ctx->stores->length, ctx->atomics->length,
                        ctx->channels->length, ctx->narrowings->length};
}

void checker_restore(CheckContext *ctx, Checkpoint checkpoint)
{
    ctx->types->length = checkpoint.types;
    ctx->functions->length = checkpoint.functions;
    ctx->struct_instantiations->length = checkpoint.structs;
    ctx->function_instantiations->length = checkpoint.instantiations;
    ctx->locals->length = checkpoint.locals;
    ctx->globals->length = checkpoint.globals;
    ctx->stores->length = checkpoint.stores;
    ctx->atomics->length = checkpoint.atomics;
    ctx->channels->length = checkpoint.channels;
    ctx->narrowings->length = checkpoint.narrowings;
    name_index_rebuild(&ctx->function_index, ctx->functions);
}

size_t *mapping_slot(List *mapping, size_t index)
{
    return (size_t *)list_get(mapping, index);
}

bool check_int32_expression(CheckContext *ctx, size_t ast_index, size_t *out)
{
    size_t value;
    size_t type;

    return check_expression(ctx, ast_index, &value) &&
           builtin_type(ctx, NYX_TYPE_INT32, &type) &&
           inject_into(ctx, value, type, out);
}

bool check_expression_coerced(CheckContext *ctx, size_t ast_index,
                              size_t expected, size_t *out)
{
    size_t value;

    if (!check_expression_expected(ctx, ast_index, expected, &value)) {
        return false;
    }
    if (expected != TYPED_NO_NODE) {
        return inject_into(ctx, value, expected, out);
    }
    if (!check_default_numeric(ctx, value)) {
        return false;
    }
    *out = value;
    return true;
}

bool check_expression_expected(CheckContext *ctx, size_t ast_index,
                               size_t expected_type, size_t *out_typed)
{
    size_t previous_ast = ctx->expected_ast;
    size_t previous_type = ctx->expected_type;
    bool ok;

    ctx->expected_ast = ast_index;
    ctx->expected_type = expected_type;
    ok = check_expression(ctx, ast_index, out_typed);
    ctx->expected_ast = previous_ast;
    ctx->expected_type = previous_type;
    return ok;
}


static bool node_is_call(const AstNode *node)
{
    return node->kind == AST_CALL || node->kind == AST_MEMBER_CALL;
}

/* A call may change what the code around it learned about a variable. The
   one exception is print, which reads. */
static void invalidate_for_call(CheckContext *ctx, const AstNode *node)
{
    const char *called = node->kind == AST_CALL
                             ? ast_symbol_text(ctx->ast, node->name_index)
                             : NULL;

    if (node->kind == AST_MEMBER_CALL) {
        invalidate_access_narrowing(ctx, node->left_child);
    }
    if (node_is_call(node) && (called == NULL || strcmp(called, "print") != 0)) {
        invalidate_aliased_narrowings(ctx);
    }
}

/* A call that yields nothing may only stand alone as a statement. */
static bool call_yields_value(CheckContext *ctx, const AstNode *node,
                              size_t ast_index, size_t typed)
{
    const TypedNode *result;
    const TypedType *type;

    if (!node_is_call(node) || ast_index == ctx->statement_call_ast) {
        return true;
    }
    result = typed_program_node(ctx->program, typed);
    type = result == NULL ? NULL
                          : typed_program_type(ctx->program, result->type_index);
    if (type == NULL || type->kind != NYX_TYPE_NIL) {
        return true;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_VOID_VALUE);
    return false;
}

/* What a checked expression changes around it: narrowing a member or index
   read, forgetting what a call may have changed, and refusing a value-less
   call used as a value. */
static bool settle_expression(CheckContext *ctx, const AstNode *node,
                              size_t ast_index, const size_t *slot, size_t *out)
{
    bool access = node->kind == AST_MEMBER_ACCESS || node->kind == AST_INDEX;

    if (access && ast_index != ctx->assignment_target_ast &&
        !apply_access_narrowing(ctx, ast_index)) {
        return false;
    }
    invalidate_for_call(ctx, node);
    *out = *slot;
    return call_yields_value(ctx, node, ast_index, *out);
}

/* A failed allocation is recorded once and the check carries on: the checker
   stops descending at the next expression or statement (both refuse to run
   after a fatal error) and the result is discarded, so a branch per
   allocation buys nothing. Returns `allocated` for the few callers that need
   it. */
bool checker_ok(ErrorState *errors, bool allocated, const char *message)
{
    if (!allocated && !errors->fatal) {
        error_report_fatal(errors, message);
    }
    return allocated;
}

/* Allocation failure leaves no reliable state for recovery, and a full
   message buffer leaves no room to report more. */
bool checker_cannot_recover(const ErrorState *errors)
{
    return errors->fatal || errors->length >= ERROR_MESSAGE_CAPACITY - 1;
}

static bool expression_match(CheckContext *ctx, const AstNode *node,
                             size_t ast_index)
{
    return check_match(ctx, node, ast_index, true, TYPED_NO_NODE, &(size_t){0});
}

static const struct {
    AstNodeKind kind;
    NodeCheck check;
} expression_checks[] = {
    {AST_INTEGER_LITERAL, check_literal},
    {AST_FLOAT_LITERAL, check_literal},
    {AST_BOOL_LITERAL, check_bool_literal},
    {AST_NIL_LITERAL, check_nil_literal},
    {AST_STRING_LITERAL, check_string_literal},
    {AST_LIST_LITERAL, check_list_literal},
    {AST_RECORD_LITERAL, check_record_literal},
    {AST_INDEX, check_index_expression},
    {AST_MEMBER_ACCESS, check_member_access},
    {AST_IS, check_is},
    {AST_IF_EXPRESSION, check_if_expression},
    {AST_MATCH_EXPRESSION, expression_match},
    {AST_MEMBER_CALL, check_member_call},
    {AST_UNARY, check_unary},
    {AST_IDENTIFIER, check_identifier},
    {AST_BINARY, check_binary},
    {AST_CALL, check_call},
};

static NodeCheck expression_check_for(AstNodeKind kind)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(expression_checks); i++) {
        if (expression_checks[i].kind == kind) {
            return expression_checks[i].check;
        }
    }
    return NULL;
}

static bool check_expression_node(CheckContext *ctx, size_t ast_index,
                                  size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t *slot;
    NodeCheck check;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    invalidate_accesses_for_expression(ctx, ast_index);
    slot = mapping_slot(ctx->mapping, ast_index);
    if (*slot != TYPED_NO_NODE) {
        *out_typed = *slot;
        return true;
    }
    check = expression_check_for(node->kind);
    if (check == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    return check(ctx, node, ast_index) &&
           settle_expression(ctx, node, ast_index, slot, out_typed);
}

/* Copy a name into the typed program. A failed copy is recorded and yields
   TYPED_NO_NODE; the node that carries the name is then refused (emit_node),
   so callers do not test it. */
size_t checker_name(CheckContext *ctx, const char *text)
{
    size_t index = typed_program_copy_symbol(ctx->program, text, strlen(text));

    checker_ok(ctx->errors, index != TYPED_NO_NODE,
               MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
    return index;
}

/* A block node over children already appended. */
bool push_block(CheckContext *ctx, SourcePos pos, size_t start,
                       size_t count, size_t *out)
{
    TypedNode block = new_node(TYPED_BLOCK, TYPED_NO_NODE, pos, TYPED_NO_NODE,
                               TYPED_NO_NODE);

    block.children_start = start;
    block.children_count = count;
    return checker_push_node(ctx, &block, out,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* Allocations that produce an index the caller reads next. They refuse to run
   after a failed allocation, because the data they would read is incomplete. */
bool checker_push_node(CheckContext *ctx, const TypedNode *node, size_t *out,
                       const char *message)
{
    return !ctx->errors->fatal &&
           checker_ok(ctx->errors, typed_program_push(ctx->program, node, out),
                      message);
}

bool checker_add_type(CheckContext *ctx, const TypedType *type, size_t *out)
{
    return !ctx->errors->fatal &&
           checker_ok(ctx->errors,
                      typed_program_add_type(ctx->program, type, out),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

bool checker_add_signature(CheckContext *ctx,
                           const TypedFunctionSignature *signature, size_t *out)
{
    return !ctx->errors->fatal &&
           checker_ok(ctx->errors,
                      typed_program_add_signature(ctx->program, signature, out),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

bool check_expression(CheckContext *ctx, size_t ast_index, size_t *out_typed)
{
    return !ctx->errors->fatal &&
           check_expression_node(ctx, ast_index, out_typed) &&
           !ctx->errors->fatal;
}


static bool check_statement_locals_start(CheckContext *ctx, const char *name, const AstNode *node, size_t *int32_type, VariableEntry *entry, size_t *locals_start)
{
    (*locals_start) = ctx->locals->length;
    (*entry) = (VariableEntry){0};
    if (name == NULL || name_is_taken(ctx, name, true)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name == NULL ? "" : name);
        return false;
    }
    return check_reserved_name(ctx, node->pos, name) &&
           check_negative_capacity(ctx, node->left_child) &&
           builtin_type(ctx, NYX_TYPE_INT32, int32_type);
}

static bool check_statement_step(const AstNode *node, CheckContext *ctx, size_t return_type, size_t *expression, const TypedNode **expression_node, size_t *coerced)
{
    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_RETURN_TYPE_MISMATCH,
                        ctx->function_name == NULL ? MESSAGE_FRAGMENT_FUNCTION
                                                   : ctx->function_name);
        return false;
    }
    if (!check_expression_expected(ctx, node->left_child, return_type,
                                   expression)) {
        return false;
    }
    (*expression_node) = typed_program_node(ctx->program, (*expression));
    if ((*expression_node) == NULL ||
        !inject_into(ctx, (*expression), return_type, coerced)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_RETURN_TYPE_MISMATCH,
                        ctx->function_name == NULL ? MESSAGE_FRAGMENT_FUNCTION
                                                   : ctx->function_name);
        return false;
    }
    return true;
}

static bool check_statement_entry(CheckContext *ctx, const char *name, const AstNode *node, size_t *int32_type, size_t *capacity, size_t *store_type, size_t return_type, size_t *body)
{
    VariableEntry entry = {0};
    bool checked = {0};
    size_t locals_start = {0};
    if (!check_statement_locals_start(ctx, name, node, int32_type, &entry, &locals_start)) {
    return false;
}
    if (!check_expression_coerced(ctx, node->left_child, (*int32_type), capacity)) {
        if (!ctx->errors->reported)
            error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_SCOPE_CAPACITY);
        return false;
    }
    if (!builtin_type(ctx, NYX_TYPE_STORE, store_type)) return false;
    memcpy(entry.name, name, strlen(name) + 1);
    entry.type_index = (*store_type);
    entry.is_const = true;
    checker_ok(ctx->errors, list_push(ctx->locals, &entry), MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checked = check_block(ctx, node->right_child, return_type, body);
    ctx->locals->length = locals_start;
    if (!checked) return false;
    return true;
}

/* A deferred block runs later, when its enclosing block exits, so nothing the
   code around it has learned about a variable still holds: a `typeof` test
   that narrowed `x` before the defer says nothing about `x` when the block
   runs. The body is checked with no narrowings, and it cannot leave itself by
   return, break, or continue because those would cut the unwinding short. */
static bool check_defer_body(CheckContext *ctx, const AstNode *node,
                             size_t return_type, size_t *out_body)
{
    List *saved_narrowings = ctx->narrowings;
    size_t saved_loop = ctx->loop_depth;
    List fresh;
    bool ok;

    list_init(&fresh, sizeof(Narrowing));
    ctx->narrowings = &fresh;
    ctx->loop_depth = 0;
    ctx->defer_depth++;
    ok = check_block(ctx, node->left_child, return_type, out_body);
    ctx->defer_depth--;
    ctx->loop_depth = saved_loop;
    ctx->narrowings = saved_narrowings;
    list_free(&fresh);
    return ok;
}

/* A node with the fields every node sets; callers add the rest. */
TypedNode new_node(TypedNodeKind kind, size_t type_index, SourcePos pos,
                   size_t left, size_t right)
{
    TypedNode typed = {0};

    typed.kind = kind;
    typed.type_index = type_index;
    typed.name_index = TYPED_NO_NODE;
    typed.pos = pos;
    typed.left_child = left;
    typed.right_child = right;
    return typed;
}

static bool statement_return_unit(CheckContext *ctx, const AstNode *node,
                                  size_t ast_index, size_t return_type,
                                  size_t *out)
{
    TypedNode unit = new_node(TYPED_NIL_LITERAL, return_type, node->pos,
                                    TYPED_NO_NODE, TYPED_NO_NODE);
    size_t expression;
    TypedNode typed;

    if (node->left_child != AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_VOID_RETURN);
        return false;
    }
    if (!checker_push_node(ctx, &unit, &expression,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    typed = new_node(TYPED_RETURN, return_type, node->pos, expression,
                           TYPED_NO_NODE);
    return emit_node(ctx, &typed, ast_index, out);
}

static bool statement_return(CheckContext *ctx, const AstNode *node,
                             size_t ast_index, size_t return_type, size_t *out)
{
    const TypedType *result = typed_program_type(ctx->program, return_type);
    const TypedNode *expression_node;
    size_t expression;
    size_t coerced;
    TypedNode typed;

    if (ctx->defer_depth > 0) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DEFER_EXIT);
        return false;
    }
    if (result != NULL && result->kind == NYX_TYPE_NIL) {
        return statement_return_unit(ctx, node, ast_index, return_type, out);
    }
    if (!check_statement_step(node, ctx, return_type, &expression,
                              &expression_node, &coerced)) {
        return false;
    }
    typed = new_node(TYPED_RETURN, return_type, node->pos, coerced,
                           TYPED_NO_NODE);
    return emit_node(ctx, &typed, ast_index, out);
}

/* The else block, checked under the negation of the condition. */
static bool check_else_branch(CheckContext *ctx, const AstNode *node,
                              size_t return_type, size_t narrowing_count,
                              bool then_ok, size_t *else_block)
{
    size_t else_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                node->children_start + 1);
    bool ok;

    if (!then_ok) {
        ctx->errors->reported = false;
    }
    ok = push_else_narrowing(ctx, node->left_child) &&
         check_block(ctx, else_ast, return_type, else_block);
    ctx->narrowings->length = narrowing_count;
    return ok;
}

/* The then block under the condition, then the else block. A bad then block
   is recorded and the else block is still checked, so one pass reports both. */
static bool check_if_branches(CheckContext *ctx, const AstNode *node,
                              size_t return_type, size_t *then_block,
                              size_t *else_block, bool *then_ok)
{
    size_t narrowing_count = ctx->narrowings->length;
    Checkpoint checkpoint = checker_checkpoint(ctx);
    size_t then_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                node->children_start);

    if (!push_if_narrowing(ctx, node->left_child)) {
        return false;
    }
    *then_ok = check_block(ctx, then_ast, return_type, then_block);
    ctx->narrowings->length = narrowing_count;
    if (!*then_ok) {
        if (checker_cannot_recover(ctx->errors)) {
            return false;
        }
        checker_restore(ctx, checkpoint);
    }
    return node->children_count <= 1 ||
           check_else_branch(ctx, node, return_type, narrowing_count, *then_ok,
                             else_block);
}

static bool statement_if(CheckContext *ctx, const AstNode *node,
                         size_t ast_index, size_t return_type, size_t *out)
{
    size_t else_block = TYPED_NO_NODE;
    size_t condition;
    size_t then_block;
    bool then_ok;
    TypedNode typed;

    if (!check_condition(ctx, node->left_child, &condition)) {
        return false;
    }
    if (node->children_count == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (!check_if_branches(ctx, node, return_type, &then_block, &else_block,
                           &then_ok)) {
        return false;
    }
    if (!then_ok) {
        ctx->errors->reported = true;
        return false;
    }
    typed = new_node(TYPED_IF, TYPED_NO_NODE, node->pos, condition,
                           TYPED_NO_NODE);
    typed.children_start = ctx->program->children.length;
    typed.children_count = node->children_count > 1 ? 2 : 1;
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, then_block),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    if (else_block != TYPED_NO_NODE) {
        checker_ok(ctx->errors,
                   typed_program_push_child(ctx->program, else_block),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return emit_node(ctx, &typed, ast_index, out);
}

static bool statement_match(CheckContext *ctx, const AstNode *node,
                            size_t ast_index, size_t return_type, size_t *out)
{
    return check_match(ctx, node, ast_index, false, return_type, out);
}

static bool statement_scope(CheckContext *ctx, const AstNode *node,
                            size_t ast_index, size_t return_type, size_t *out)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t capacity, int32_type, store_type, body;
    TypedNode typed;

    if (!check_statement_entry(ctx, name, node, &int32_type, &capacity,
                               &store_type, return_type, &body)) {
        return false;
    }
    typed = new_node(TYPED_SCOPE, store_type, node->pos, capacity, body);
    typed.name_index = typed_program_copy_symbol(ctx->program, name,
                                                 strlen(name));
    typed.store_id = TYPED_NO_NODE;
    if (typed.name_index == TYPED_NO_NODE) {
        error_report_fatal(ctx->errors,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
        return false;
    }
    return emit_node(ctx, &typed, ast_index, out);
}

static bool statement_parallel(CheckContext *ctx, const AstNode *node,
                               size_t ast_index, size_t return_type,
                               size_t *out)
{
    return check_parallel(ctx, node, ast_index, return_type, out);
}

static bool statement_when(CheckContext *ctx, const AstNode *node,
                           size_t ast_index, size_t return_type, size_t *out)
{
    return check_when(ctx, node, ast_index, return_type, out);
}

static bool statement_defer(CheckContext *ctx, const AstNode *node,
                            size_t ast_index, size_t return_type, size_t *out)
{
    size_t body;
    TypedNode typed;

    if (!check_defer_body(ctx, node, return_type, &body)) {
        return false;
    }
    typed = new_node(TYPED_DEFER, TYPED_NO_NODE, node->pos, body,
                           TYPED_NO_NODE);
    return emit_node(ctx, &typed, ast_index, out);
}

static bool statement_while(CheckContext *ctx, const AstNode *node,
                            size_t ast_index, size_t return_type, size_t *out)
{
    size_t condition;
    size_t body;
    bool checked;
    TypedNode typed;

    if (!check_condition(ctx, node->left_child, &condition)) {
        return false;
    }
    ctx->loop_depth++;
    checked = check_block(ctx, node->right_child, return_type, &body);
    ctx->loop_depth--;
    if (!checked) {
        return false;
    }
    typed = new_node(TYPED_WHILE, TYPED_NO_NODE, node->pos, condition,
                           body);
    return emit_node(ctx, &typed, ast_index, out);
}

static bool statement_declaration(CheckContext *ctx, const AstNode *node,
                                  size_t ast_index, size_t return_type,
                                  size_t *out)
{
    (void)return_type;
    return check_local_declaration(ctx, node, ast_index, out);
}

static bool statement_for(CheckContext *ctx, const AstNode *node,
                          size_t ast_index, size_t return_type, size_t *out)
{
    return check_for(ctx, node, ast_index, return_type, out);
}

static bool statement_assignment(CheckContext *ctx, const AstNode *node,
                                 size_t ast_index, size_t return_type,
                                 size_t *out)
{
    (void)return_type;
    return check_assignment(ctx, node, ast_index, out);
}

static bool statement_index_assignment(CheckContext *ctx, const AstNode *node,
                                       size_t ast_index, size_t return_type,
                                       size_t *out)
{
    (void)return_type;
    return check_index_assignment(ctx, node, ast_index, out);
}

static bool statement_member_assignment(CheckContext *ctx, const AstNode *node,
                                        size_t ast_index, size_t return_type,
                                        size_t *out)
{
    (void)return_type;
    return check_member_assignment(ctx, node, ast_index, out);
}

static bool statement_expression(CheckContext *ctx, const AstNode *node,
                                 size_t ast_index, size_t return_type,
                                 size_t *out)
{
    (void)node;
    (void)return_type;
    return check_expression_statement(ctx, ast_index, out);
}

/* break and continue: only inside a loop, and never out of a defer block. */
static bool statement_jump(CheckContext *ctx, const AstNode *node,
                           size_t ast_index, size_t return_type, size_t *out)
{
    bool is_break = node->kind == AST_BREAK;
    TypedNode typed;

    (void)return_type;
    if (ctx->loop_depth == 0) {
        error_report_at(ctx->errors, node->pos,
                        ctx->defer_depth > 0
                            ? MESSAGE_CHECKER_DEFER_EXIT
                            : is_break ? MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP
                                       : MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
        return false;
    }
    typed = new_node(is_break ? TYPED_BREAK : TYPED_CONTINUE,
                           TYPED_NO_NODE, node->pos, TYPED_NO_NODE,
                           TYPED_NO_NODE);
    return emit_node(ctx, &typed, ast_index, out);
}

typedef bool (*StatementCheck)(CheckContext *ctx, const AstNode *node,
                               size_t ast_index, size_t return_type,
                               size_t *out);

static const struct {
    AstNodeKind kind;
    StatementCheck check;
} statement_checks[] = {
    {AST_RETURN, statement_return},
    {AST_IF, statement_if},
    {AST_MATCH, statement_match},
    {AST_SCOPE, statement_scope},
    {AST_PARALLEL, statement_parallel},
    {AST_WHEN, statement_when},
    {AST_DEFER, statement_defer},
    {AST_WHILE, statement_while},
    {AST_DECLARATION, statement_declaration},
    {AST_FOR, statement_for},
    {AST_ASSIGNMENT, statement_assignment},
    {AST_INDEX_ASSIGNMENT, statement_index_assignment},
    {AST_MEMBER_ASSIGNMENT, statement_member_assignment},
    {AST_EXPRESSION_STATEMENT, statement_expression},
    {AST_BREAK, statement_jump},
    {AST_CONTINUE, statement_jump},
};

static bool check_new_node(CheckContext *ctx, size_t ast_index,
                                 size_t return_type, size_t *out_typed)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(statement_checks); i++) {
        if (statement_checks[i].kind == node->kind) {
            return statement_checks[i].check(ctx, node, ast_index, return_type,
                                             out_typed);
        }
    }
    error_report_at(ctx->errors, node->pos,
                    MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}

/* Both entry points refuse to start after a failed allocation and report
   failure if one happened inside, so the inconsistent state a failed
   allocation leaves is never read by the caller. */
bool check_statement(CheckContext *ctx, size_t ast_index, size_t return_type,
                     size_t *out_typed)
{
    return !ctx->errors->fatal &&
           check_new_node(ctx, ast_index, return_type, out_typed) &&
           !ctx->errors->fatal;
}



bool emit_node(CheckContext *ctx, const TypedNode *node,
                      size_t ast_index, size_t *out_index)
{
    /* Every typed node is built here, so a node that follows a failed
       allocation is where the check stops. */
    if (ctx->errors->fatal ||
        !checker_push_node(ctx, node, out_index, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = *out_index;
    return true;
}

bool push_injection(CheckContext *ctx, size_t value, size_t target,
                           size_t tag, size_t *out_value)
{
    const TypedNode *value_node = typed_program_node(ctx->program, value);
    TypedNode inject = {0};

    inject.kind = TYPED_SUM_INJECT;
    inject.type_index = target;
    inject.pos = value_node == NULL ? (SourcePos){0, 1, 1} : value_node->pos;
    inject.integer_value = (int)tag;
    inject.name_index = TYPED_NO_NODE;
    inject.left_child = value;
    inject.right_child = TYPED_NO_NODE;
    if (!checker_push_node(ctx, &inject, out_value, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    return true;
}




/* The lists a check owns. They live in one table so opening and closing them
   are loops, and a new list is one enum value and one element size. */
typedef enum {
    CL_MAPPING,
    CL_TYPES,
    CL_FUNCTIONS,
    CL_GLOBALS,
    CL_LOCALS,
    CL_STRUCT_INSTANTIATIONS,
    CL_FUNCTION_INSTANTIATIONS,
    CL_NARROWINGS,
    CL_LAYOUT_CACHE,
    CL_STORES,
    CL_ATOMICS,
    CL_CHANNELS,
    CL_INVALID_ROOTS,
    CL_FOLD_MEMO,
    CL_COUNT
} CheckList;

static const size_t check_list_sizes[CL_COUNT] = {
    [CL_MAPPING] = sizeof(size_t),
    [CL_TYPES] = sizeof(TypeEntry),
    [CL_FUNCTIONS] = sizeof(FunctionEntry),
    [CL_GLOBALS] = sizeof(VariableEntry),
    [CL_LOCALS] = sizeof(VariableEntry),
    [CL_STRUCT_INSTANTIATIONS] = sizeof(StructInstantiation),
    [CL_FUNCTION_INSTANTIATIONS] = sizeof(FunctionInstantiation),
    [CL_NARROWINGS] = sizeof(Narrowing),
    [CL_LAYOUT_CACHE] = sizeof(ValueLayoutCache),
    [CL_STORES] = sizeof(StoreEntry),
    [CL_ATOMICS] = sizeof(AtomicEntry),
    [CL_CHANNELS] = sizeof(ChannelEntry),
    [CL_INVALID_ROOTS] = sizeof(bool),
    [CL_FOLD_MEMO] = sizeof(FoldMemo),
};

static void checker_open(CheckContext *ctx, List *lists, const Ast *ast,
                         TypedProgram *program, ErrorState *errors,
                         const TypeEnv *empty_env)
{
    size_t i;

    for (i = 0; i < CL_COUNT; i++) {
        list_init(&lists[i], check_list_sizes[i]);
    }
    *ctx = (CheckContext){
        .ast = ast,
        .program = program,
        .errors = errors,
        .mapping = &lists[CL_MAPPING],
        .types = &lists[CL_TYPES],
        .functions = &lists[CL_FUNCTIONS],
        .globals = &lists[CL_GLOBALS],
        .locals = &lists[CL_LOCALS],
        .struct_instantiations = &lists[CL_STRUCT_INSTANTIATIONS],
        .function_instantiations = &lists[CL_FUNCTION_INSTANTIATIONS],
        .narrowings = &lists[CL_NARROWINGS],
        .layout_cache = &lists[CL_LAYOUT_CACHE],
        .stores = &lists[CL_STORES],
        .atomics = &lists[CL_ATOMICS],
        .channels = &lists[CL_CHANNELS],
        .fold_memo = &lists[CL_FOLD_MEMO],
        .fold_epoch = 1,
        .statement_call_ast = AST_NO_NODE,
        .expected_ast = AST_NO_NODE,
        .expected_type = TYPED_NO_NODE,
        .assignment_target_ast = AST_NO_NODE,
        .env = empty_env,
    };
    name_index_init(&ctx->function_index);
}

static void checker_close(CheckContext *ctx, List *lists)
{
    size_t i;

    for (i = 0; i < CL_COUNT; i++) {
        list_free(&lists[i]);
    }
    name_index_free(&ctx->function_index);
}

/* The two per-root and per-node lists must be complete before anything reads
   them, so these loops stop at the first failed push and the check ends. */
static bool checker_seed_roots(CheckContext *ctx, List *invalid_roots)
{
    size_t none = TYPED_NO_NODE;
    bool valid = false;
    size_t i;

    checker_ok(ctx->errors, list_push(&ctx->program->match_scopes, &none),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    for (i = 0; i < ctx->ast->roots.length && !ctx->errors->fatal; i++) {
        checker_ok(ctx->errors, list_push(invalid_roots, &valid),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

static bool checker_seed_nodes(CheckContext *ctx)
{
    size_t none = TYPED_NO_NODE;
    FoldMemo unfolded = {0};
    size_t i;

    for (i = 0; i < ctx->ast->nodes.length && !ctx->errors->fatal; i++) {
        checker_ok(ctx->errors, list_push(ctx->mapping, &none),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        checker_ok(ctx->errors, list_push(ctx->fold_memo, &unfolded),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    return !ctx->errors->fatal;
}

/* Declare every type before any body is checked. Returns false when the
   check cannot go on. */
static bool checker_predeclare_types(CheckContext *ctx, List *invalid_roots,
                                     bool *had_errors)
{
    size_t i;

    ctx->predeclaring_types = true;
    for (i = 0; i < ctx->ast->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&ctx->ast->roots, i);
        const AstNode *node = ast_get_node(ctx->ast, root);
        size_t before = ctx->types->length;
        bool declares = node != NULL && (node->kind == AST_TYPE_DECLARATION ||
                                         node->kind == AST_TYPE_ALIAS);

        if (!declares || *(const bool *)list_get(invalid_roots, i)) {
            continue;
        }
        ctx->errors->reported = false;
        if (check_type_declaration(ctx, node, root)) {
            continue;
        }
        if (checker_cannot_recover(ctx->errors)) {
            return false;
        }
        ctx->types->length = before;
        *(bool *)list_get(invalid_roots, i) = true;
        *had_errors = true;
    }
    ctx->predeclaring_types = false;
    return true;
}

static bool root_type(CheckContext *ctx, const AstNode *node, size_t root)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    size_t index = find_type_index(ctx->types, name);
    TypeEntry *entry = (TypeEntry *)list_get(ctx->types, index);
    size_t resolved;
    bool checked;

    (void)root;
    if (entry == NULL || entry->parameter_count != 0) {
        return entry != NULL;
    }
    checked = entry->kind == TYPE_ENTRY_ALIAS
                  ? expand_alias(ctx, entry, NULL, 0, &resolved)
                  : instantiate_struct(ctx, index, NULL, 0, &resolved);
    checked = checked && check_type_value_size(ctx, resolved, node->pos);
    if (checked) {
        entry->type_index = resolved;
    }
    return checked;
}

static bool root_expression(CheckContext *ctx, const AstNode *node, size_t root)
{
    (void)node;
    return check_expression_statement(ctx, root, &(size_t){0});
}

static bool root_statement(CheckContext *ctx, const AstNode *node, size_t root)
{
    (void)node;
    return check_statement(ctx, root, TYPED_NO_NODE, &(size_t){0});
}

static bool root_defer(CheckContext *ctx, const AstNode *node, size_t root)
{
    (void)root;
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DEFER_TOP_LEVEL);
    return false;
}

static bool root_for(CheckContext *ctx, const AstNode *node, size_t root)
{
    return check_for(ctx, node, root, TYPED_NO_NODE, &(size_t){0});
}

static bool root_assignment(CheckContext *ctx, const AstNode *node, size_t root)
{
    return check_assignment(ctx, node, root, &(size_t){0});
}

static bool root_index_assignment(CheckContext *ctx, const AstNode *node,
                                  size_t root)
{
    return check_index_assignment(ctx, node, root, &(size_t){0});
}

static bool root_member_assignment(CheckContext *ctx, const AstNode *node,
                                   size_t root)
{
    return check_member_assignment(ctx, node, root, &(size_t){0});
}

static const struct {
    AstNodeKind kind;
    NodeCheck check;
} root_checks[] = {
    {AST_DECLARATION, check_var_declaration},
    {AST_TYPE_DECLARATION, root_type},
    {AST_TYPE_ALIAS, root_type},
    {AST_FUNCTION_DECLARATION, check_function_declaration},
    {AST_EXPRESSION_STATEMENT, root_expression},
    {AST_IF, root_statement},
    {AST_MATCH, root_statement},
    {AST_SCOPE, root_statement},
    {AST_WHILE, root_statement},
    {AST_PARALLEL, root_statement},
    {AST_WHEN, root_statement},
    {AST_DEFER, root_defer},
    {AST_FOR, root_for},
    {AST_ASSIGNMENT, root_assignment},
    {AST_INDEX_ASSIGNMENT, root_index_assignment},
    {AST_MEMBER_ASSIGNMENT, root_member_assignment},
};

static NodeCheck root_check_for(AstNodeKind kind)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(root_checks); i++) {
        if (root_checks[i].kind == kind) {
            return root_checks[i].check;
        }
    }
    return NULL;
}

/* A checked root that is a function body or a statement joins the program's
   roots. A generic function is only a template: its instantiations add their
   own concrete roots when called. */
static void checker_publish_root(CheckContext *ctx, const AstNode *node,
                                 size_t root)
{
    size_t typed_root = *mapping_slot(ctx->mapping, root);
    bool declares = node->kind == AST_TYPE_DECLARATION ||
                    node->kind == AST_TYPE_ALIAS;

    if (declares || typed_root == TYPED_NO_NODE) {
        return;
    }
    checker_ok(ctx->errors, list_push(&ctx->program->roots, &typed_root),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* Check one root. Returns false when the whole check must stop. */
static bool checker_check_root(CheckContext *ctx, List *invalid_roots, size_t i,
                               bool *had_errors)
{
    size_t root = *(const size_t *)list_get(&ctx->ast->roots, i);
    const AstNode *node = ast_get_node(ctx->ast, root);
    Checkpoint checkpoint = checker_checkpoint(ctx);
    NodeCheck check;

    if (*(const bool *)list_get(invalid_roots, i)) {
        return true;
    }
    ctx->errors->reported = false;
    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, root);
        return false;
    }
    check = root_check_for(node->kind);
    if (check == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    if (check(ctx, node, root)) {
        checker_publish_root(ctx, node, root);
        return !ctx->errors->fatal;
    }
    if (!ctx->errors->reported) {
        error_unknown_if_none(ctx->errors);
    }
    if (checker_cannot_recover(ctx->errors)) {
        return false;
    }
    checker_restore(ctx, checkpoint);
    *had_errors = true;
    return true;
}

/* Whole-program rules that need every type: a Handle's payload must not be a
   fixed-size value, and no by-value type may exceed the size cap. */
static bool checker_check_type(CheckContext *ctx, size_t index, bool *failed)
{
    const TypedProgram *program = ctx->program;
    const TypedType *type = typed_program_type(program, index);
    const TypedNode *declaration;
    SourcePos pos = {0, 1, 1};
    size_t element;
    bool valid = true;

    if (type == NULL) {
        return true;
    }
    if (type->kind == NYX_TYPE_HANDLE && type->child_count == 1) {
        element = *(const size_t *)list_get(&program->type_children,
                                            type->first_child);
        if (!check_handle_payload(ctx, element, &valid)) {
            return false;
        }
        if (!valid) {
            error_report(ctx->errors, MESSAGE_CHECKER_HANDLE_FIXED);
            *failed = true;
            return true;
        }
    }
    if (!is_sized_type(program, index)) {
        return true;
    }
    declaration = type->kind == NYX_TYPE_STRUCT
                      ? typed_program_node(program, type->declaration_node)
                      : NULL;
    pos = declaration == NULL ? pos : declaration->pos;
    *failed = !check_type_value_size(ctx, index, pos);
    return true;
}

/* Returns false when the check cannot go on; `failed` reports a rejected
   program. */
static bool checker_check_types(CheckContext *ctx, bool *failed)
{
    size_t i;

    for (i = 0; i < ctx->program->types.length && !*failed; i++) {
        if (!checker_check_type(ctx, i, failed)) {
            return false;
        }
    }
    return true;
}

static bool checker_check_roots(CheckContext *ctx, List *invalid_roots,
                                bool *had_errors)
{
    size_t i;

    for (i = 0; i < ctx->ast->roots.length; i++) {
        if (!checker_check_root(ctx, invalid_roots, i, had_errors)) {
            return false;
        }
    }
    return true;
}

static bool checker_run(CheckContext *ctx, List *invalid_roots)
{
    bool had_errors = false;

    if (!checker_seed_roots(ctx, invalid_roots)) {
        return false;
    }
    had_errors = !validate_source_types(ctx, invalid_roots);
    if (!checker_seed_nodes(ctx) ||
        !checker_predeclare_types(ctx, invalid_roots, &had_errors) ||
        !checker_check_roots(ctx, invalid_roots, &had_errors) ||
        (!had_errors && !checker_check_types(ctx, &had_errors))) {
        return false;
    }
    ctx->errors->reported = ctx->errors->reported || had_errors;
    return !had_errors && !ctx->errors->fatal;
}

bool checker_check(const Ast *ast, TypedProgram *program, ErrorState *errors)
{
    CheckContext ctx;
    TypeEnv empty_env = {0};
    List lists[CL_COUNT];
    bool ok;

    if (ast->roots.length == 0) {
        error_report(errors, MESSAGE_CHECKER_MISSING_AST_ROOT);
        return false;
    }
    checker_open(&ctx, lists, ast, program, errors, &empty_env);
    ok = checker_run(&ctx, &lists[CL_INVALID_ROOTS]);
    checker_close(&ctx, lists);
    return ok;
}
