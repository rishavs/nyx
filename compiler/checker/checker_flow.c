#include "checker_internal.h"


/* Questions about how control leaves a typed statement (does it always
   return, always stop, ever break the loop around it) share one walk. A query
   is a table saying, for each kind of statement, how its answer follows from
   the statement's parts; a kind the table leaves out answers no. */
typedef struct FlowQuery FlowQuery;
typedef bool (*FlowRule)(const TypedProgram *program, size_t index,
                         const TypedNode *node, const FlowQuery *query);
typedef struct {
    TypedNodeKind kind;
    FlowRule rule;
} FlowEntry;
struct FlowQuery {
    const FlowEntry *entries;
    size_t count;
};

static bool flow_holds(const TypedProgram *program, size_t index,
                       const FlowQuery *query)
{
    const TypedNode *node = typed_program_node(program, index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    for (i = 0; i < query->count; i++) {
        if (query->entries[i].kind == node->kind) {
            return query->entries[i].rule(program, index, node, query);
        }
    }
    return false;
}

static size_t flow_child(const TypedProgram *program, const TypedNode *node,
                         size_t i)
{
    return *(const size_t *)list_get(&program->children,
                                     node->children_start + i);
}

/* An `if` has a then and maybe an else; a `when` has its arms and maybe an
   else. */
static size_t flow_branch_count(const TypedNode *node)
{
    return node->kind == TYPED_IF
               ? node->children_count
               : node->children_count + (node->right_child != TYPED_NO_NODE);
}

static size_t flow_branch(const TypedProgram *program, const TypedNode *node,
                          size_t i)
{
    const TypedNode *arm;

    if (node->kind == TYPED_IF) {
        return flow_child(program, node, i);
    }
    if (i == node->children_count) {
        return node->right_child;
    }
    arm = typed_program_node(program, flow_child(program, node, i));
    return arm == NULL ? TYPED_NO_NODE : arm->right_child;
}

static bool flow_yes(const TypedProgram *program, size_t index,
                     const TypedNode *node, const FlowQuery *query)
{
    (void)program;
    (void)index;
    (void)node;
    (void)query;
    return true;
}

static bool flow_no(const TypedProgram *program, size_t index,
                    const TypedNode *node, const FlowQuery *query)
{
    (void)program;
    (void)index;
    (void)node;
    (void)query;
    return false;
}

/* The answer is the answer for the statement inside (a scope, a match arm). */
static bool flow_through(const TypedProgram *program, size_t index,
                         const TypedNode *node, const FlowQuery *query)
{
    (void)index;
    return flow_holds(program, node->right_child, query);
}

static bool flow_any_child(const TypedProgram *program, size_t index,
                           const TypedNode *node, const FlowQuery *query)
{
    size_t i;

    (void)index;
    for (i = 0; i < node->children_count; i++) {
        if (flow_holds(program, flow_child(program, node, i), query)) {
            return true;
        }
    }
    return false;
}

static bool flow_any_branch(const TypedProgram *program, size_t index,
                            const TypedNode *node, const FlowQuery *query)
{
    size_t i;

    (void)index;
    for (i = 0; i < flow_branch_count(node); i++) {
        if (flow_holds(program, flow_branch(program, node, i), query)) {
            return true;
        }
    }
    return false;
}

/* Exactly one branch runs, so the answer holds only if it holds for every one.
   An `if` without an else has a branch that does nothing. */
static bool flow_all_branches(const TypedProgram *program, size_t index,
                              const TypedNode *node, const FlowQuery *query)
{
    size_t i;

    (void)index;
    if (node->kind == TYPED_IF && node->children_count != 2) {
        return false;
    }
    for (i = 0; i < flow_branch_count(node); i++) {
        if (!flow_holds(program, flow_branch(program, node, i), query)) {
            return false;
        }
    }
    return true;
}

static const FlowEntry breaks_loop_entries[] = {
    {TYPED_BREAK, flow_yes},
    {TYPED_SCOPE, flow_through},
    {TYPED_MATCH_BIND_STATEMENT, flow_through},
    {TYPED_WHILE, flow_no},
    {TYPED_FOR, flow_no},
    {TYPED_WHEN, flow_any_branch},
    {TYPED_BLOCK, flow_any_child},
    {TYPED_IF, flow_any_branch},
};
static const FlowQuery breaks_loop_query = {breaks_loop_entries,
                                            NYX_COUNT_OF(breaks_loop_entries)};

/* `while true` with no break never reaches the statement after it. */
static bool flow_endless_loop(const TypedProgram *program, size_t index,
                              const TypedNode *node, const FlowQuery *query)
{
    const TypedNode *condition;

    (void)index;
    (void)query;
    if (node->left_child == TYPED_NO_NODE ||
        node->right_child == TYPED_NO_NODE) {
        return false;
    }
    condition = typed_program_node(program, node->left_child);
    return condition != NULL && condition->kind == TYPED_BOOL_LITERAL &&
           condition->integer_value != 0 &&
           !flow_holds(program, node->right_child, &breaks_loop_query);
}

static bool flow_block_returns(const TypedProgram *program, size_t index,
                               const TypedNode *node, const FlowQuery *query)
{
    (void)node;
    (void)query;
    return typed_block_returns(program, index);
}

static const FlowEntry returns_entries[] = {
    {TYPED_RETURN, flow_yes},
    {TYPED_SCOPE, flow_through},
    {TYPED_MATCH_BIND_STATEMENT, flow_through},
    {TYPED_BLOCK, flow_block_returns},
    {TYPED_WHEN, flow_all_branches},
    {TYPED_WHILE, flow_endless_loop},
    {TYPED_IF, flow_all_branches},
};
static const FlowQuery returns_query = {returns_entries,
                                        NYX_COUNT_OF(returns_entries)};

static const FlowEntry terminates_entries[] = {
    {TYPED_RETURN, flow_yes},
    {TYPED_BREAK, flow_yes},
    {TYPED_CONTINUE, flow_yes},
    {TYPED_SCOPE, flow_through},
    {TYPED_MATCH_BIND_STATEMENT, flow_through},
    {TYPED_BLOCK, flow_any_child},
    {TYPED_WHILE, flow_endless_loop},
    {TYPED_WHEN, flow_all_branches},
    {TYPED_IF, flow_all_branches},
};
static const FlowQuery terminates_query = {terminates_entries,
                                           NYX_COUNT_OF(terminates_entries)};

bool typed_statement_returns(const TypedProgram *program, size_t index)
{
    return flow_holds(program, index, &returns_query);
}

bool typed_statement_terminates(const TypedProgram *program, size_t index)
{
    return flow_holds(program, index, &terminates_query);
}

bool typed_block_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *block = typed_program_node(program, index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (typed_statement_returns(program, child)) {
            return true;
        }
        if (typed_statement_terminates(program, child)) {
            return false;
        }
    }
    return false;
}


bool check_condition(CheckContext *ctx, size_t ast_index,
                            size_t *out_typed)
{
    size_t expression;
    const TypedNode *node;
    const TypedType *type;

    if (!check_expression(ctx, ast_index, &expression)) {
        return false;
    }
    node = typed_program_node(ctx->program, expression);
    type = node == NULL ? NULL
                        : typed_program_type(ctx->program, node->type_index);
    if (type == NULL || type->kind != NYX_TYPE_BOOL) {
        const AstNode *ast_node = ast_get_node(ctx->ast, ast_index);

        error_report_at(ctx->errors,
                        ast_node == NULL ? (SourcePos){0, 1, 1} : ast_node->pos,
                        MESSAGE_CHECKER_CONDITION_TYPE);
        return false;
    }
    *out_typed = expression;
    return true;
}

static const char *access_root(const Ast *ast, size_t index)
{
    const AstNode *root = access_root_node(ast, index);

    return root == NULL ? NULL : ast_symbol_text(ast, root->name_index);
}

/* Expressions that are chains of operators over names and literals are
   compared and searched by their shape: a table says which operands of each
   kind of node are themselves expressions and which payload makes two nodes of
   a kind equal. */
#define SHAPE_LEFT 1u
#define SHAPE_RIGHT 2u

typedef enum {
    PAYLOAD_NONE,
    PAYLOAD_NAME,
    PAYLOAD_VALUE,
    PAYLOAD_OP
} Payload;

typedef struct {
    AstNodeKind kind;
    unsigned operands;
    Payload payload;
} AstShape;

static const AstShape *shape_of(const AstShape *shapes, size_t count,
                                AstNodeKind kind)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (shapes[i].kind == kind) {
            return &shapes[i];
        }
    }
    return NULL;
}

static bool operand_holds(unsigned operands, unsigned side, const Ast *ast,
                          size_t child, bool (*test)(const Ast *, size_t))
{
    return (operands & side) == 0 || test(ast, child);
}

/* An index that cannot change between the test and the use: names, integer
   literals, and operators over them. */
static const AstShape subscript_shapes[] = {
    {AST_IDENTIFIER, 0, PAYLOAD_NAME},
    {AST_INTEGER_LITERAL, 0, PAYLOAD_VALUE},
    {AST_UNARY, SHAPE_LEFT, PAYLOAD_OP},
    {AST_BINARY, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_OP},
};

static bool stable_subscript(const Ast *ast, size_t index)
{
    const AstNode *node = ast_get_node(ast, index);
    const AstShape *shape =
        node == NULL ? NULL
                     : shape_of(subscript_shapes,
                                NYX_COUNT_OF(subscript_shapes), node->kind);

    return shape != NULL &&
           operand_holds(shape->operands, SHAPE_LEFT, ast, node->left_child,
                         stable_subscript) &&
           operand_holds(shape->operands, SHAPE_RIGHT, ast, node->right_child,
                         stable_subscript);
}

static bool same_payload(Payload payload, const AstNode *a, const AstNode *b)
{
    return payload == PAYLOAD_NONE ||
           (payload == PAYLOAD_NAME && a->name_index == b->name_index) ||
           (payload == PAYLOAD_VALUE &&
            a->integer_value_u == b->integer_value_u) ||
           (payload == PAYLOAD_OP && a->op == b->op);
}

static bool same_subscript(const Ast *ast, size_t a, size_t b)
{
    const AstNode *left = ast_get_node(ast, a);
    const AstNode *right = ast_get_node(ast, b);
    const AstShape *shape;

    if (left == NULL || right == NULL || left->kind != right->kind) {
        return false;
    }
    shape = shape_of(subscript_shapes, NYX_COUNT_OF(subscript_shapes),
                     left->kind);
    return shape != NULL && same_payload(shape->payload, left, right) &&
           ((shape->operands & SHAPE_LEFT) == 0 ||
            same_subscript(ast, left->left_child, right->left_child)) &&
           ((shape->operands & SHAPE_RIGHT) == 0 ||
            same_subscript(ast, left->right_child, right->right_child));
}

static bool same_access(const Ast *ast, size_t a, size_t b)
{
    const AstNode *left = ast_get_node(ast, a);
    const AstNode *right = ast_get_node(ast, b);

    if (left == NULL || right == NULL || left->kind != right->kind) return false;
    if (left->kind == AST_IDENTIFIER) return left->name_index == right->name_index;
    if (left->kind == AST_MEMBER_ACCESS)
        return left->name_index == right->name_index &&
               same_access(ast, left->left_child, right->left_child);
    if (left->kind == AST_INDEX) {
        return same_subscript(ast, left->right_child, right->right_child) &&
               same_access(ast, left->left_child, right->left_child);
    }
    return false;
}

/* The list a subscript reads from, if the base has been checked. */
static const TypedType *index_base_type(const CheckContext *ctx,
                                        const AstNode *node)
{
    const TypedNode *base = typed_program_node(
        ctx->program, *mapping_slot(ctx->mapping, node->left_child));

    return base == NULL ? NULL
                        : typed_program_type(ctx->program, base->type_index);
}

static bool stable_access(CheckContext *ctx, size_t index)
{
    const AstNode *node = ast_get_node(ctx->ast, index);
    const TypedType *base;

    if (node == NULL) {
        return false;
    }
    if (node->kind == AST_IDENTIFIER) {
        return true;
    }
    if (node->kind == AST_MEMBER_ACCESS) {
        return stable_access(ctx, node->left_child);
    }
    if (node->kind != AST_INDEX) {
        return false;
    }
    base = index_base_type(ctx, node);
    return base != NULL &&
           (base->kind == NYX_TYPE_FIXED_LIST || base->kind == NYX_TYPE_LIST) &&
           stable_subscript(ctx->ast, node->right_child) &&
           stable_access(ctx, node->left_child);
}

static bool access_has_list(CheckContext *ctx, size_t index)
{
    const AstNode *node = ast_get_node(ctx->ast, index);
    const TypedType *base;

    if (node == NULL || node->kind == AST_IDENTIFIER) {
        return false;
    }
    base = node->kind == AST_INDEX ? index_base_type(ctx, node) : NULL;
    return (base != NULL && base->kind == NYX_TYPE_LIST) ||
           access_has_list(ctx, node->left_child);
}

void invalidate_aliased_narrowings(CheckContext *ctx)
{
    size_t i;
    for (i = 0; i < ctx->narrowings->length; i++) {
        Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);
        if (narrowing->alias_sensitive) narrowing->active = false;
    }
}

static const AstShape access_shapes[] = {
    {AST_MEMBER_ACCESS, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_INDEX, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
    {AST_UNARY, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_BINARY, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
};

/* Whether the access reads the variable `name` anywhere in its path. */
static bool access_depends_on(const Ast *ast, size_t index, const char *name)
{
    const AstNode *node = ast_get_node(ast, index);
    const AstShape *shape;
    const char *symbol;

    if (node == NULL) {
        return false;
    }
    if (node->kind == AST_IDENTIFIER) {
        symbol = ast_symbol_text(ast, node->name_index);
        return symbol != NULL && strcmp(symbol, name) == 0;
    }
    shape = shape_of(access_shapes, NYX_COUNT_OF(access_shapes), node->kind);
    return shape != NULL &&
           (((shape->operands & SHAPE_LEFT) != 0 &&
             access_depends_on(ast, node->left_child, name)) ||
            ((shape->operands & SHAPE_RIGHT) != 0 &&
             access_depends_on(ast, node->right_child, name)));
}

/* A call can mutate an earlier field or element before the condition ends. */
static const AstShape pure_shapes[] = {
    {AST_IDENTIFIER, 0, PAYLOAD_NONE},
    {AST_INTEGER_LITERAL, 0, PAYLOAD_NONE},
    {AST_FLOAT_LITERAL, 0, PAYLOAD_NONE},
    {AST_BOOL_LITERAL, 0, PAYLOAD_NONE},
    {AST_NIL_LITERAL, 0, PAYLOAD_NONE},
    {AST_STRING_LITERAL, 0, PAYLOAD_NONE},
    {AST_UNARY, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_IS, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_MEMBER_ACCESS, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_BINARY, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
    {AST_INDEX, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
};

/* A call can mutate an earlier field or element before the condition ends. */
static bool condition_is_pure(const Ast *ast, size_t index)
{
    const AstNode *node = ast_get_node(ast, index);
    const AstShape *shape =
        node == NULL ? NULL
                     : shape_of(pure_shapes, NYX_COUNT_OF(pure_shapes),
                                node->kind);

    return shape != NULL &&
           operand_holds(shape->operands, SHAPE_LEFT, ast, node->left_child,
                         condition_is_pure) &&
           operand_holds(shape->operands, SHAPE_RIGHT, ast, node->right_child,
                         condition_is_pure);
}

/* The operands of an expression that can hold a call. */
static const AstShape call_shapes[] = {
    {AST_MEMBER_CALL, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_MEMBER_ACCESS, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_UNARY, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_IS, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_NAMED_ARG, SHAPE_LEFT, PAYLOAD_NONE},
    {AST_BINARY, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
    {AST_INDEX, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
    {AST_IF_EXPRESSION, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
    {AST_MATCH_EXPRESSION, SHAPE_LEFT | SHAPE_RIGHT, PAYLOAD_NONE},
};

static bool contains_mutating_call(const Ast *ast, size_t index,
                                   bool include_self, bool include_functions);

/* A method call may mutate its receiver; a plain call may mutate anything it
   can reach, except print, which only reads. */
static bool is_mutating_call(const Ast *ast, const AstNode *node,
                             bool include_functions)
{
    const char *name = node->kind == AST_CALL
                           ? ast_symbol_text(ast, node->name_index)
                           : NULL;

    return node->kind == AST_MEMBER_CALL ||
           (node->kind == AST_CALL && include_functions &&
            (name == NULL || strcmp(name, "print") != 0));
}

static bool operands_mutate(const Ast *ast, const AstNode *node,
                            bool include_functions)
{
    const AstShape *shape =
        shape_of(call_shapes, NYX_COUNT_OF(call_shapes), node->kind);
    unsigned operands = shape == NULL ? 0 : shape->operands;

    return ((operands & SHAPE_LEFT) != 0 &&
            contains_mutating_call(ast, node->left_child, true,
                                   include_functions)) ||
           ((operands & SHAPE_RIGHT) != 0 &&
            contains_mutating_call(ast, node->right_child, true,
                                   include_functions));
}

static bool contains_mutating_call(const Ast *ast, size_t index,
                                   bool include_self, bool include_functions)
{
    const AstNode *node = ast_get_node(ast, index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    if (include_self && is_mutating_call(ast, node, include_functions)) {
        return true;
    }
    if (operands_mutate(ast, node, include_functions)) {
        return true;
    }
    for (i = 0; i < node->children_count; i++) {
        const size_t *child = list_get(&ast->children, node->children_start + i);

        if (child != NULL &&
            contains_mutating_call(ast, *child, true, include_functions)) {
            return true;
        }
    }
    return false;
}

/* Whether any narrowing of a member or element is live, and whether any of
   those can be reached through another name. */
static bool narrowing_paths_live(const CheckContext *ctx, bool *aliased)
{
    bool active = false;
    size_t i;

    *aliased = false;
    for (i = 0; i < ctx->narrowings->length; i++) {
        const Narrowing *narrowing = list_get(ctx->narrowings, i);
        bool live = narrowing->active && narrowing->path_ast != AST_NO_NODE;

        active = active || live;
        *aliased = *aliased || (live && narrowing->alias_sensitive);
    }
    return active;
}

/* An expression with a method call can change any member or element it
   narrowed; one with a plain call can also change what an alias narrowed. */
static void deactivate_narrowings(CheckContext *ctx, bool has_member,
                                  bool has_call)
{
    size_t i;

    for (i = 0; i < ctx->narrowings->length; i++) {
        Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);

        if ((has_member && narrowing->path_ast != AST_NO_NODE) ||
            (has_call && narrowing->alias_sensitive)) {
            narrowing->active = false;
        }
    }
}

/* An expression with a method call can change any member or element it
   narrowed; one with a plain call can also change what an alias narrowed. */
void invalidate_accesses_for_expression(CheckContext *ctx, size_t ast_index)
{
    bool aliased;

    if (narrowing_paths_live(ctx, &aliased)) {
        deactivate_narrowings(
            ctx, contains_mutating_call(ctx->ast, ast_index, false, false),
            aliased && contains_mutating_call(ctx->ast, ast_index, false, true));
    }
}

void invalidate_access_narrowing(CheckContext *ctx, size_t ast_index)
{
    const char *root = access_root(ctx->ast, ast_index);
    size_t i;
    if (root == NULL) return;
    for (i = 0; i < ctx->narrowings->length; i++) {
        Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);
        if (narrowing->active && narrowing->path_ast != AST_NO_NODE &&
            strcmp(narrowing->name, root) == 0) narrowing->active = false;
    }
}

static TypedNode payload_node(SourcePos pos, size_t type_index, size_t tag,
                             size_t source)
{
    TypedNode payload = {0};

    payload.kind = TYPED_SUM_PAYLOAD;
    payload.type_index = type_index;
    payload.pos = pos;
    payload.name_index = TYPED_NO_NODE;
    payload.integer_value = (int)tag;
    payload.left_child = source;
    payload.right_child = TYPED_NO_NODE;
    return payload;
}

/* Alternative `i` of the narrowed union, taken out of the source value and
   put back into the narrowed union type. */
static bool narrowed_alternative(CheckContext *ctx, size_t source, SourcePos pos,
                                 const Narrowing *narrowing, size_t i,
                                 size_t *injected, size_t *alternative)
{
    const TypedType *original =
        typed_program_type(ctx->program, narrowing->original_type);
    const TypedType *target =
        typed_program_type(ctx->program, narrowing->type_index);
    size_t payload_index;
    size_t tag;
    TypedNode payload;

    *alternative = *(const size_t *)list_get(&ctx->program->type_children,
                                             target->first_child + i);
    if (original == NULL ||
        !union_has_alternative(ctx->program, original, *alternative, &tag)) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    payload = payload_node(pos, *alternative, tag, source);
    return checker_push_node(ctx, &payload, &payload_index,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM) &&
           push_injection(ctx, payload_index, narrowing->type_index, i,
                          injected);
}

/* `if source is alternative then <injected> else <chain>`. */
static bool narrowed_select(CheckContext *ctx, size_t source, SourcePos pos,
                            const Narrowing *narrowing, size_t bool_type,
                            size_t alternative, size_t injected, size_t *chain)
{
    TypedNode test = {0};
    TypedNode select = {0};
    size_t condition;

    test.kind = TYPED_IS;
    test.type_index = bool_type;
    test.auxiliary_type = alternative;
    test.name_index = TYPED_NO_NODE;
    test.pos = pos;
    test.left_child = source;
    test.right_child = TYPED_NO_NODE;
    if (!checker_push_node(ctx, &test, &condition,
                           MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    select.kind = TYPED_IF_EXPRESSION;
    select.type_index = narrowing->type_index;
    select.name_index = TYPED_NO_NODE;
    select.pos = pos;
    select.left_child = condition;
    select.right_child = injected;
    select.children_start = ctx->program->children.length;
    select.children_count = 1;
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, *chain),
               MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    return checker_push_node(ctx, &select, chain,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
}

/* The value of a narrowed variable. A single alternative is its payload; a
   narrower union is chosen at run time by testing which alternative the
   source holds. */
bool emit_narrowed_value(CheckContext *ctx, size_t source,
                         const Narrowing *narrowing, size_t *out)
{
    const TypedNode *source_node = typed_program_node(ctx->program, source);
    const TypedType *target =
        typed_program_type(ctx->program, narrowing->type_index);
    size_t chain = TYPED_NO_NODE;
    size_t bool_type;
    size_t count;
    size_t i;
    SourcePos pos;

    if (source_node == NULL || target == NULL) {
        return false;
    }
    pos = source_node->pos;
    count = target->child_count;
    if (target->kind != NYX_TYPE_UNION) {
        TypedNode payload =
            payload_node(pos, narrowing->type_index, narrowing->tag, source);

        return checker_push_node(ctx, &payload, out,
                                 MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    }
    if (!builtin_type(ctx, NYX_TYPE_BOOL, &bool_type)) {
        return false;
    }
    for (i = count; i > 0; i--) {
        size_t injected;
        size_t alternative;

        if (!narrowed_alternative(ctx, source, pos, narrowing, i - 1, &injected,
                                  &alternative)) {
            return false;
        }
        if (chain == TYPED_NO_NODE) {
            chain = injected;
        } else if (!narrowed_select(ctx, source, pos, narrowing, bool_type,
                                    alternative, injected, &chain)) {
            return false;
        }
    }
    *out = chain;
    return true;
}

bool apply_access_narrowing(CheckContext *ctx, size_t ast_index)
{
    size_t i;
    for (i = ctx->narrowings->length; i > 0; i--) {
        const Narrowing *narrowing = list_get(ctx->narrowings, i - 1);
        if (narrowing->active && narrowing->path_ast != AST_NO_NODE &&
            same_access(ctx->ast, narrowing->path_ast, ast_index)) {
            size_t result;
            if (!emit_narrowed_value(ctx, *mapping_slot(ctx->mapping, ast_index),
                                     narrowing, &result)) return false;
            *mapping_slot(ctx->mapping, ast_index) = result;
            return true;
        }
    }
    return true;
}

static bool push_condition_narrowing(CheckContext *ctx, size_t condition_ast,
                                     bool negated, bool allow_paths);

/* Two facts narrow the same thing: the same variable, or the same path
   through it. */
static bool same_fact_target(const CheckContext *ctx, const Narrowing *a,
                             const Narrowing *b)
{
    bool a_path = a->path_ast != AST_NO_NODE;
    bool b_path = b->path_ast != AST_NO_NODE;

    return strcmp(a->name, b->name) == 0 && a_path == b_path &&
           (!a_path || same_access(ctx->ast, a->path_ast, b->path_ast));
}

/* The last fact of `facts` that narrows what `left` narrows from the same
   starting type. */
static const Narrowing *matching_fact(const CheckContext *ctx, const List *facts,
                                      const Narrowing *left)
{
    size_t j;

    for (j = facts->length; j > 0; j--) {
        const Narrowing *candidate = list_get(facts, j - 1);

        if (left->original_type == candidate->original_type &&
            same_fact_target(ctx, left, candidate)) {
            return candidate;
        }
    }
    return NULL;
}

static bool has_later_fact(const CheckContext *ctx, const List *facts,
                           size_t from, const Narrowing *left)
{
    size_t j;

    for (j = from; j < facts->length; j++) {
        if (same_fact_target(ctx, left, list_get(facts, j))) {
            return true;
        }
    }
    return false;
}

/* What is known after either branch of an `or`: the union of what each
   branch knew, unless that is no narrower than the declared type. */
static bool join_facts(CheckContext *ctx, Narrowing left, const Narrowing *right)
{
    const TypedType *combined_type;
    const TypedType *original;
    List alternatives;
    size_t combined;
    bool ok;

    list_init(&alternatives, sizeof(size_t));
    checker_ok(ctx->errors, list_push(&alternatives, &left.type_index),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    checker_ok(ctx->errors, list_push(&alternatives, &right->type_index),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    ok = !ctx->errors->fatal && intern_union(ctx, &alternatives, &combined);
    list_free(&alternatives);
    if (!ok) {
        return false;
    }
    if (same_type(ctx->program, combined, left.original_type)) {
        return true;
    }
    left.type_index = combined;
    combined_type = typed_program_type(ctx->program, combined);
    original = typed_program_type(ctx->program, left.original_type);
    if (combined_type->kind != NYX_TYPE_UNION &&
        !union_has_alternative(ctx->program, original, combined, &left.tag)) {
        return true;
    }
    return checker_ok(ctx->errors, list_push(ctx->narrowings, &left),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

static bool join_one(CheckContext *ctx, const List *left_facts,
                     const List *right_facts, size_t index)
{
    Narrowing left = *(const Narrowing *)list_get(left_facts, index);
    const Narrowing *right;

    if (has_later_fact(ctx, left_facts, index + 1, &left)) {
        return true;
    }
    right = matching_fact(ctx, right_facts, &left);
    return right == NULL || join_facts(ctx, left, right);
}

/* The facts one side of a join pushed, moved out of the narrowing list. */
static bool collect_facts(CheckContext *ctx, size_t condition_ast, bool negated,
                          bool paths, size_t start, List *facts)
{
    bool ok = push_condition_narrowing(ctx, condition_ast, negated, paths);
    size_t i;

    for (i = start; ok && i < ctx->narrowings->length; i++) {
        ok = checker_ok(ctx->errors, list_push(facts, list_get(ctx->narrowings, i)),
                        MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    ctx->narrowings->length = start;
    return ok;
}

/* `a or b` (or the negation of `a and b`) learns only what both sides agree
   on. */
static bool push_join(CheckContext *ctx, const AstNode *condition,
                      size_t condition_ast, bool negated, bool allow_paths)
{
    size_t start = ctx->narrowings->length;
    bool paths = allow_paths && condition_is_pure(ctx->ast, condition_ast);
    List left_facts;
    List right_facts;
    size_t i;
    bool ok;

    list_init(&left_facts, sizeof(Narrowing));
    list_init(&right_facts, sizeof(Narrowing));
    ok = collect_facts(ctx, condition->left_child, negated, paths, start,
                       &left_facts) &&
         collect_facts(ctx, condition->right_child, negated, paths, start,
                       &right_facts);
    for (i = left_facts.length; ok && i > 0; i--) {
        ok = join_one(ctx, &left_facts, &right_facts, i - 1);
    }
    list_free(&left_facts);
    list_free(&right_facts);
    if (!ok) {
        ctx->narrowings->length = start;
    }
    return ok;
}

/* A variable's type here: the declared one, or what an enclosing test already
   narrowed it to. */
const Narrowing *prior_narrowing(const CheckContext *ctx, const char *name,
                                bool path, size_t access)
{
    size_t i;

    for (i = ctx->narrowings->length; i > 0; i--) {
        const Narrowing *prior = list_get(ctx->narrowings, i - 1);
        bool same_path = path ? prior->path_ast != AST_NO_NODE &&
                                    same_access(ctx->ast, prior->path_ast, access)
                              : prior->path_ast == AST_NO_NODE;

        if (prior->active && strcmp(prior->name, name) == 0 && same_path) {
            return prior;
        }
    }
    return NULL;
}

/* What an `is` test narrows: the variable at the root of the tested access,
   its type before the test, and the type it had before any narrowing. */
typedef struct {
    const char *name;
    size_t original_type;
    size_t tested; /* the type at the test, narrowed so far */
    const TypedType *declared;
} NarrowTarget;

static bool narrow_target(CheckContext *ctx, const AstNode *condition,
                          const AstNode *receiver, NarrowTarget *target)
{
    bool path = receiver->kind != AST_IDENTIFIER;
    const char *name = access_root(ctx->ast, condition->left_child);
    const VariableEntry *variable =
        name == NULL ? NULL : lookup_variable(ctx, name);
    const TypedNode *value = NULL;
    const Narrowing *prior;
    size_t type_index;

    if (variable == NULL) {
        return false;
    }
    type_index = variable->type_index;
    if (path) {
        value = typed_program_node(
            ctx->program, *mapping_slot(ctx->mapping, condition->left_child));
        if (value == NULL) {
            return false;
        }
        type_index = value->type_index;
    }
    target->original_type = type_index;
    prior = prior_narrowing(ctx, name, path, condition->left_child);
    if (prior != NULL) {
        type_index = prior->type_index;
        target->original_type = prior->original_type;
    }
    target->name = name;
    target->declared = typed_program_type(ctx->program, type_index);
    target->tested = type_index;
    return true;
}

/* After a failed `is`, the value holds any alternative but the tested one. */
static bool complement_union(CheckContext *ctx, const TypedType *declared,
                             size_t tag, size_t *tested)
{
    size_t first = declared->first_child;
    size_t count = declared->child_count;
    List alternatives;
    size_t i;
    bool ok;

    list_init(&alternatives, sizeof(size_t));
    for (i = 0; i < count; i++) {
        size_t item = *(const size_t *)list_get(&ctx->program->type_children,
                                                first + i);

        if (i != tag) {
            checker_ok(ctx->errors, list_push(&alternatives, &item),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    ok = !ctx->errors->fatal && intern_union(ctx, &alternatives, tested);
    list_free(&alternatives);
    return ok;
}

static bool push_narrowing_fact(CheckContext *ctx, const NarrowTarget *target,
                                const AstNode *receiver,
                                const AstNode *condition, size_t tag)
{
    bool path = receiver->kind != AST_IDENTIFIER;
    Narrowing narrowing = {0};

    narrowing.name = target->name;
    narrowing.path_ast = path ? condition->left_child : AST_NO_NODE;
    narrowing.type_index = target->tested;
    narrowing.original_type = target->original_type;
    narrowing.tag = tag;
    narrowing.active = true;
    narrowing.alias_sensitive = path && access_has_list(ctx, condition->left_child);
    return checker_ok(ctx->errors, list_push(ctx->narrowings, &narrowing),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* `x is T`: x has type T where the test held (or any other alternative where
   it failed). Only a union can be narrowed. */
/* Whether the tested type narrows the variable's original union: it is one
   of its alternatives, or a narrower union. Sets `tag` for an alternative. */
static bool narrowing_applies(CheckContext *ctx, const NarrowTarget *target,
                              size_t *tag)
{
    const TypedType *original =
        typed_program_type(ctx->program, target->original_type);

    return typed_program_type(ctx->program, target->tested)->kind ==
               NYX_TYPE_UNION ||
           (original != NULL &&
            union_has_alternative(ctx->program, original, target->tested, tag));
}

/* The target of an `is` test, when it is a union: its declared type and the
   tested type. Returns false when there is nothing to narrow. */
static bool union_target(CheckContext *ctx, const AstNode *condition,
                         const AstNode *receiver, NarrowTarget *target)
{
    return narrow_target(ctx, condition, receiver, target) &&
           target->declared != NULL && target->declared->kind == NYX_TYPE_UNION;
}

/* `x is T`: x has type T where the test held (or any other alternative where
   it failed). Only a union can be narrowed. */
static bool push_is_fact(CheckContext *ctx, const AstNode *condition,
                         const AstNode *receiver, bool negated)
{
    NarrowTarget target;
    size_t declared_index;
    size_t tag;

    if (!union_target(ctx, condition, receiver, &target)) {
        return true;
    }
    declared_index = target.tested;
    if (!resolve_type_in_env(ctx, condition->right_child, ctx->env, "is",
                             &target.tested)) {
        return false;
    }
    target.declared = typed_program_type(ctx->program, declared_index);
    if (target.declared == NULL ||
        !union_has_alternative(ctx->program, target.declared, target.tested,
                               &tag)) {
        return true;
    }
    if (negated && !complement_union(ctx, target.declared, tag, &target.tested)) {
        return false;
    }
    return !narrowing_applies(ctx, &target, &tag) ||
           push_narrowing_fact(ctx, &target, receiver, condition, tag);
}

/* `a and b` (or the negation of `a or b`) learns what both sides learn. The
   left side may name a path only if the right side cannot change it. */
static bool push_conjunction(CheckContext *ctx, const AstNode *condition,
                             bool negated, bool allow_paths)
{
    bool left_paths =
        allow_paths && condition_is_pure(ctx->ast, condition->right_child);

    return push_condition_narrowing(ctx, condition->left_child, negated,
                                    left_paths) &&
           push_condition_narrowing(ctx, condition->right_child, negated,
                                    allow_paths);
}

/* Whether the condition is `op` once a negation is pushed inward: not (a and
   b) is (not a) or (not b). */
static bool effective_op_is(const AstNode *condition, NyxOperator op,
                            bool negated)
{
    NyxOperator dual = op == NYX_OPERATOR_AND ? NYX_OPERATOR_OR
                                              : NYX_OPERATOR_AND;

    return condition != NULL && condition->kind == AST_BINARY &&
           condition->op == (negated ? dual : op);
}

/* `not` flips a condition's sense without changing what it tests. */
static const AstNode *strip_negations(const CheckContext *ctx, size_t *ast,
                                      bool *negated)
{
    const AstNode *condition = ast_get_node(ctx->ast, *ast);

    while (condition != NULL && condition->kind == AST_UNARY &&
           condition->op == NYX_OPERATOR_NOT) {
        *negated = !*negated;
        *ast = condition->left_child;
        condition = ast_get_node(ctx->ast, *ast);
    }
    return condition;
}

/* The receiver of an `is` is a name, or a stable path when paths are allowed. */
static const AstNode *usable_receiver(CheckContext *ctx, const AstNode *condition,
                                      bool allow_paths)
{
    const AstNode *receiver = ast_get_node(ctx->ast, condition->left_child);
    bool path_ok = allow_paths && stable_access(ctx, condition->left_child);

    return receiver != NULL && (receiver->kind == AST_IDENTIFIER || path_ok)
               ? receiver
               : NULL;
}

static bool push_condition_narrowing(CheckContext *ctx, size_t condition_ast,
                                     bool negated, bool allow_paths)
{
    const AstNode *condition = strip_negations(ctx, &condition_ast, &negated);
    const AstNode *receiver;

    if (effective_op_is(condition, NYX_OPERATOR_OR, negated)) {
        return push_join(ctx, condition, condition_ast, negated, allow_paths);
    }
    if (effective_op_is(condition, NYX_OPERATOR_AND, negated)) {
        return push_conjunction(ctx, condition, negated, allow_paths);
    }
    if (condition == NULL || condition->kind != AST_IS) {
        return true;
    }
    receiver = usable_receiver(ctx, condition, allow_paths);
    return receiver == NULL || push_is_fact(ctx, condition, receiver, negated);
}

bool push_if_narrowing(CheckContext *ctx, size_t condition_ast)
{
    return push_condition_narrowing(ctx, condition_ast, false, true);
}

bool push_else_narrowing(CheckContext *ctx, size_t condition_ast)
{
    return push_condition_narrowing(ctx, condition_ast, true, true);
}



void invalidate_narrowing(CheckContext *ctx, const char *name)
{
    size_t i;

    for (i = 0; i < ctx->narrowings->length; i++) {
        Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);

        if (narrowing != NULL && narrowing->active &&
            narrowing->name != NULL &&
            (strcmp(narrowing->name, name) == 0 ||
             (narrowing->path_ast != AST_NO_NODE &&
              access_depends_on(ctx->ast, narrowing->path_ast, name)))) {
            /* Mark it inactive rather than removing it. The if-block
               restoration truncates by length; removing entries would let a
               nested block resurrect a stale narrowing. */
            narrowing->active = false;
        }
    }
}


/* One arm of an if expression, checked under its narrowing. */
static bool check_if_arm(CheckContext *ctx, const AstNode *node, size_t arm,
                         size_t expected, size_t narrowing_start,
                         size_t *result)
{
    bool ok = check_expression_coerced(ctx, arm, expected, result);

    ctx->narrowings->length = narrowing_start;
    if (!ok && !ctx->errors->reported) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
    }
    return ok;
}

static bool emit_if_expression(CheckContext *ctx, const AstNode *node,
                               size_t ast_index, size_t condition,
                               size_t then_expression, size_t else_expression)
{
    const TypedNode *then_node = typed_program_node(ctx->program, then_expression);
    const TypedNode *else_node = typed_program_node(ctx->program, else_expression);
    TypedNode typed = {0};

    if (then_node == NULL || else_node == NULL ||
        !same_type(ctx->program, then_node->type_index,
                   else_node->type_index)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
        return false;
    }
    typed.kind = TYPED_IF_EXPRESSION;
    typed.type_index = then_node->type_index;
    typed.pos = node->pos;
    typed.name_index = TYPED_NO_NODE;
    typed.left_child = condition;
    typed.right_child = then_expression;
    typed.auxiliary_type = TYPED_NO_NODE;
    typed.store_id = TYPED_NO_NODE;
    typed.children_start = ctx->program->children.length;
    typed.children_count = 1;
    checker_ok(ctx->errors,
               typed_program_push_child(ctx->program, else_expression),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}

/* The then arm. A bad one is recorded and the else arm is still checked, so one
   pass reports both; returns false when the check cannot go on. */
static bool check_then_arm(CheckContext *ctx, const AstNode *node,
                           size_t expected, size_t narrowing_start,
                           Checkpoint checkpoint, size_t *expression,
                           bool *then_ok)
{
    *then_ok = check_if_arm(ctx, node, node->right_child, expected,
                            narrowing_start, expression);
    if (*then_ok) {
        return true;
    }
    if (checker_cannot_recover(ctx->errors)) {
        return false;
    }
    checker_restore(ctx, checkpoint);
    ctx->errors->reported = false;
    return true;
}

/* Both arms are checked even if the first is wrong, so one pass reports both. */
bool check_if_expression(CheckContext *ctx, const AstNode *node,
                         size_t ast_index)
{
    size_t expected = ctx->expected_ast == ast_index ? ctx->expected_type
                                                     : TYPED_NO_NODE;
    size_t else_ast = *(const size_t *)list_get(&ctx->ast->children,
                                                node->children_start);
    size_t condition;
    size_t then_expression;
    size_t else_expression;
    size_t narrowing_start;
    Checkpoint checkpoint;
    bool then_ok;

    if (!check_condition(ctx, node->left_child, &condition)) {
        return false;
    }
    narrowing_start = ctx->narrowings->length;
    checkpoint = checker_checkpoint(ctx);
    if (!push_if_narrowing(ctx, node->left_child)) {
        ctx->narrowings->length = narrowing_start;
        return false;
    }
    if (!check_then_arm(ctx, node, expected, narrowing_start, checkpoint,
                        &then_expression, &then_ok) ||
        !push_else_narrowing(ctx, node->left_child) ||
        !check_if_arm(ctx, node, else_ast, expected, narrowing_start,
                      &else_expression)) {
        return false;
    }
    ctx->errors->reported = ctx->errors->reported || !then_ok;
    return then_ok && emit_if_expression(ctx, node, ast_index, condition,
                                         then_expression, else_expression);
}


/* The type a match head has: a variable's declared type, or the type of the
   already checked path. TYPED_NO_NODE when there is none. */
static size_t match_head_type(const CheckContext *ctx, const AstNode *head,
                              size_t head_ast, const VariableEntry *variable)
{
    const TypedNode *value;

    if (head->kind == AST_IDENTIFIER) {
        return variable->type_index;
    }
    value = typed_program_node(ctx->program,
                               *mapping_slot(ctx->mapping, head_ast));
    return value == NULL ? TYPED_NO_NODE : value->type_index;
}

/* Inside a match arm the head has the arm's type, when the head is a union
   held in a variable or a stable path through one. */
/* The variable a match head reads, when the head is a name or a stable path
   through one. */
static const VariableEntry *head_variable(CheckContext *ctx, size_t head_ast,
                                          const char **name)
{
    const AstNode *head = ast_get_node(ctx->ast, head_ast);
    bool usable = head != NULL && (head->kind == AST_IDENTIFIER ||
                                   stable_access(ctx, head_ast));

    *name = usable ? access_root(ctx->ast, head_ast) : NULL;
    return *name == NULL ? NULL : lookup_variable(ctx, *name);
}

static void push_match_fact(CheckContext *ctx, const char *name, bool path,
                            size_t head_ast, size_t original_type, size_t tested,
                            size_t tag)
{
    Narrowing narrowing = {0};

    narrowing.name = name;
    narrowing.path_ast = path ? head_ast : AST_NO_NODE;
    narrowing.type_index = tested;
    narrowing.original_type = original_type;
    narrowing.tag = tag;
    narrowing.active = true;
    narrowing.alias_sensitive = path && access_has_list(ctx, head_ast);
    checker_ok(ctx->errors, list_push(ctx->narrowings, &narrowing),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* Inside a match arm the head has the arm's type, when the head is a union
   held in a variable or a stable path through one. */
bool push_match_narrowing(CheckContext *ctx, size_t head_ast, size_t tested,
                          size_t tag)
{
    const char *name;
    const VariableEntry *variable = head_variable(ctx, head_ast, &name);
    const AstNode *head = ast_get_node(ctx->ast, head_ast);
    bool path = head != NULL && head->kind != AST_IDENTIFIER;
    const TypedType *declared;
    const Narrowing *prior;
    size_t original;

    if (variable == NULL) {
        return true;
    }
    original = match_head_type(ctx, head, head_ast, variable);
    declared = typed_program_type(ctx->program, original);
    if (declared == NULL || declared->kind != NYX_TYPE_UNION) {
        return true;
    }
    prior = prior_narrowing(ctx, name, path, head_ast);
    original = prior == NULL ? original : prior->original_type;
    declared = typed_program_type(ctx->program, original);
    if (declared != NULL &&
        union_has_alternative(ctx->program, declared, tested, &tag)) {
        push_match_fact(ctx, name, path, head_ast, original, tested, tag);
    }
    return true;
}
