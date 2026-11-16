#include "checker_internal.h"


static bool typed_statement_breaks_loop(const TypedProgram *program,
                                        size_t index)
{
    const TypedNode *node = typed_program_node(program, index);
    size_t i;

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_BREAK) {
        return true;
    }
    if (node->kind == TYPED_SCOPE) {
        return typed_statement_breaks_loop(program, node->right_child);
    }
    if (node->kind == TYPED_MATCH_BIND_STATEMENT) {
        return typed_statement_breaks_loop(program, node->right_child);
    }
    if (node->kind == TYPED_WHILE || node->kind == TYPED_FOR) {
        return false;
    }
    if (node->kind == TYPED_BLOCK) {
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, node->children_start + i);

            if (typed_statement_breaks_loop(program, child)) {
                return true;
            }
        }
    }
    if (node->kind == TYPED_IF && node->children_count >= 1) {
        size_t then_block = *(const size_t *)list_get(
            &program->children, node->children_start);

        if (typed_statement_breaks_loop(program, then_block)) {
            return true;
        }
        if (node->children_count == 2) {
            size_t else_block = *(const size_t *)list_get(
                &program->children, node->children_start + 1);

            return typed_statement_breaks_loop(program, else_block);
        }
    }
    return false;
}


static bool typed_while_never_falls_through(const TypedProgram *program,
                                            const TypedNode *node)
{
    const TypedNode *condition;

    if (node == NULL || node->kind != TYPED_WHILE ||
        node->left_child == TYPED_NO_NODE ||
        node->right_child == TYPED_NO_NODE) {
        return false;
    }
    condition = typed_program_node(program, node->left_child);
    return condition != NULL && condition->kind == TYPED_BOOL_LITERAL &&
           condition->integer_value != 0 &&
           !typed_statement_breaks_loop(program, node->right_child);
}


bool typed_statement_returns(const TypedProgram *program, size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN) {
        return true;
    }
    if (node->kind == TYPED_SCOPE) {
        return typed_statement_returns(program, node->right_child);
    }
    if (node->kind == TYPED_MATCH_BIND_STATEMENT) {
        return typed_statement_returns(program, node->right_child);
    }
    if (node->kind == TYPED_BLOCK) {
        return typed_block_returns(program, index);
    }
    if (node->kind == TYPED_WHILE) {
        return typed_while_never_falls_through(program, node);
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_returns(program, then_block) &&
               typed_statement_returns(program, else_block);
    }
    return false;
}


bool typed_statement_terminates(const TypedProgram *program,
                                       size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    if (node != NULL && node->kind == TYPED_SCOPE) {
        return typed_statement_terminates(program, node->right_child);
    }
    if (node != NULL && node->kind == TYPED_MATCH_BIND_STATEMENT) {
        return typed_statement_terminates(program, node->right_child);
    }

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_RETURN || node->kind == TYPED_BREAK ||
        node->kind == TYPED_CONTINUE) {
        return true;
    }
    if (node->kind == TYPED_BLOCK) {
        size_t i;

        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);

            if (typed_statement_terminates(program, child)) {
                return true;
            }
        }
        return false;
    }
    if (node->kind == TYPED_WHILE) {
        return typed_while_never_falls_through(program, node);
    }
    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block;

        if (node->children_count != 2) {
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        else_block = *(const size_t *)list_get(&program->children,
                                               node->children_start + 1);
        return typed_statement_terminates(program, then_block) &&
               typed_statement_terminates(program, else_block);
    }
    return false;
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
    if (type == NULL || type->kind != NIPPY_TYPE_BOOL) {
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
    const AstNode *node = ast_get_node(ast, index);

    while (node != NULL &&
           (node->kind == AST_MEMBER_ACCESS || node->kind == AST_INDEX)) {
        node = ast_get_node(ast, node->left_child);
    }
    return node != NULL && node->kind == AST_IDENTIFIER
               ? ast_symbol_text(ast, node->name_index) : NULL;
}

static bool stable_subscript(const Ast *ast, size_t index)
{
    const AstNode *node = ast_get_node(ast, index);
    if (node == NULL) return false;
    if (node->kind == AST_IDENTIFIER || node->kind == AST_INTEGER_LITERAL)
        return true;
    if (node->kind == AST_UNARY)
        return stable_subscript(ast, node->left_child);
    if (node->kind == AST_BINARY)
        return stable_subscript(ast, node->left_child) &&
               stable_subscript(ast, node->right_child);
    return false;
}

static bool same_subscript(const Ast *ast, size_t a, size_t b)
{
    const AstNode *left = ast_get_node(ast, a);
    const AstNode *right = ast_get_node(ast, b);
    if (left == NULL || right == NULL || left->kind != right->kind) return false;
    if (left->kind == AST_IDENTIFIER) return left->name_index == right->name_index;
    if (left->kind == AST_INTEGER_LITERAL)
        return left->integer_value_u == right->integer_value_u;
    if (left->kind == AST_UNARY)
        return left->op == right->op &&
               same_subscript(ast, left->left_child, right->left_child);
    if (left->kind == AST_BINARY)
        return left->op == right->op &&
               same_subscript(ast, left->left_child, right->left_child) &&
               same_subscript(ast, left->right_child, right->right_child);
    return false;
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

static bool stable_access(CheckContext *ctx, size_t index)
{
    const AstNode *node = ast_get_node(ctx->ast, index);

    if (node == NULL) return false;
    if (node->kind == AST_IDENTIFIER) return true;
    if (node->kind == AST_MEMBER_ACCESS)
        return stable_access(ctx, node->left_child);
    if (node->kind == AST_INDEX) {
        size_t mapped = *mapping_slot(ctx->mapping, node->left_child);
        const TypedNode *base = typed_program_node(ctx->program, mapped);
        const TypedType *type = base == NULL ? NULL :
            typed_program_type(ctx->program, base->type_index);
        return type != NULL &&
               (type->kind == NIPPY_TYPE_FIXED_LIST ||
                type->kind == NIPPY_TYPE_LIST) &&
               stable_subscript(ctx->ast, node->right_child) &&
               stable_access(ctx, node->left_child);
    }
    return false;
}

static bool access_has_list(CheckContext *ctx, size_t index)
{
    const AstNode *node = ast_get_node(ctx->ast, index);
    if (node == NULL || node->kind == AST_IDENTIFIER) return false;
    if (node->kind == AST_INDEX) {
        const TypedNode *base = typed_program_node(
            ctx->program, *mapping_slot(ctx->mapping, node->left_child));
        const TypedType *type = base == NULL ? NULL :
            typed_program_type(ctx->program, base->type_index);
        if (type != NULL && type->kind == NIPPY_TYPE_LIST) return true;
    }
    return access_has_list(ctx, node->left_child);
}

void invalidate_aliased_narrowings(CheckContext *ctx)
{
    size_t i;
    for (i = 0; i < ctx->narrowings->length; i++) {
        Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);
        if (narrowing->alias_sensitive) narrowing->active = false;
    }
}

static bool access_depends_on(const Ast *ast, size_t index, const char *name)
{
    const AstNode *node = ast_get_node(ast, index);
    if (node == NULL) return false;
    if (node->kind == AST_IDENTIFIER) {
        const char *symbol = ast_symbol_text(ast, node->name_index);
        return symbol != NULL && strcmp(symbol, name) == 0;
    }
    if (node->kind == AST_MEMBER_ACCESS)
        return access_depends_on(ast, node->left_child, name);
    if (node->kind == AST_INDEX)
        return access_depends_on(ast, node->left_child, name) ||
               access_depends_on(ast, node->right_child, name);
    if (node->kind == AST_UNARY)
        return access_depends_on(ast, node->left_child, name);
    if (node->kind == AST_BINARY)
        return access_depends_on(ast, node->left_child, name) ||
               access_depends_on(ast, node->right_child, name);
    return false;
}

/* A call can mutate an earlier field or element before the condition ends. */
static bool condition_is_pure(const Ast *ast, size_t index)
{
    const AstNode *node = ast_get_node(ast, index);
    if (node == NULL) return false;
    switch (node->kind) {
    case AST_IDENTIFIER:
    case AST_INTEGER_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_BOOL_LITERAL:
    case AST_NIL_LITERAL:
    case AST_STRING_LITERAL:
        return true;
    case AST_UNARY:
    case AST_IS:
    case AST_MEMBER_ACCESS:
        return condition_is_pure(ast, node->left_child);
    case AST_BINARY:
    case AST_INDEX:
        return condition_is_pure(ast, node->left_child) &&
               condition_is_pure(ast, node->right_child);
    default:
        return false;
    }
}

static bool contains_mutating_call_name(const AstNode *node, bool include_self, bool include_functions, const Ast *ast, bool *left, bool *right)
{
    if (node->kind == AST_CALL && include_self && include_functions) {
        const char *name = ast_symbol_text(ast, node->name_index);
        if (name == NULL || strcmp(name, "print") != 0) return false;
    }
    switch (node->kind) {
    case AST_MEMBER_CALL:
    case AST_MEMBER_ACCESS:
    case AST_UNARY:
    case AST_IS:
    case AST_NAMED_ARG:
        (*left) = true;
        break;
    case AST_BINARY:
    case AST_INDEX:
    case AST_IF_EXPRESSION:
    case AST_MATCH_EXPRESSION:
        (*left) = true;
        (*right) = true;
        break;
    default:
        break;
    }
    return true;
}

static bool contains_mutating_call(const Ast *ast, size_t index,
                                   bool include_self, bool include_functions)
{
    const AstNode *node = ast_get_node(ast, index);
    size_t i;
    bool left = false;
    bool right = false;

    if (node == NULL) return false;
    if (node->kind == AST_MEMBER_CALL && include_self) return true;
    if (!contains_mutating_call_name(node, include_self, include_functions, ast, &left, &right)) {
        return true;
    }
    if ((left && contains_mutating_call(ast, node->left_child, true,
                                        include_functions)) ||
        (right && contains_mutating_call(ast, node->right_child, true,
                                         include_functions))) return true;
    for (i = 0; i < node->children_count; i++) {
        const size_t *child = list_get(&ast->children, node->children_start + i);
        if (child != NULL && contains_mutating_call(ast, *child, true,
                                                   include_functions)) return true;
    }
    return false;
}

void invalidate_accesses_for_expression(CheckContext *ctx, size_t ast_index)
{
    size_t i;
    bool active = false;
    bool aliased = false;
    for (i = 0; i < ctx->narrowings->length; i++) {
        const Narrowing *narrowing = list_get(ctx->narrowings, i);
        if (narrowing->active && narrowing->path_ast != AST_NO_NODE) {
            active = true;
            if (narrowing->alias_sensitive) aliased = true;
        }
    }
    if (!active) return;
    {
        bool has_member = contains_mutating_call(ctx->ast, ast_index, false, false);
        bool has_call = aliased &&
                        contains_mutating_call(ctx->ast, ast_index, false, true);
        if (!has_member && !has_call) return;
        for (i = 0; i < ctx->narrowings->length; i++) {
            Narrowing *narrowing = (Narrowing *)list_get(ctx->narrowings, i);
            if ((has_member && narrowing->path_ast != AST_NO_NODE) ||
                (has_call && narrowing->alias_sensitive)) narrowing->active = false;
        }
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

bool emit_narrowed_value(CheckContext *ctx, size_t source,
                         const Narrowing *narrowing, size_t *out)
{
    const TypedNode *source_node = typed_program_node(ctx->program, source);
    const TypedType *target = typed_program_type(ctx->program,
                                                 narrowing->type_index);
    SourcePos pos;
    size_t count, i, chain = TYPED_NO_NODE, bool_type;

    if (source_node == NULL || target == NULL) return false;
    pos = source_node->pos;
    if (target->kind != NIPPY_TYPE_UNION) {
        TypedNode payload = {0};
        payload.kind = TYPED_SUM_PAYLOAD;
        payload.type_index = narrowing->type_index;
        payload.pos = pos;
        payload.name_index = TYPED_NO_NODE;
        payload.integer_value = (int)narrowing->tag;
        payload.left_child = source;
        payload.right_child = TYPED_NO_NODE;
        if (!typed_program_push(ctx->program, &payload, out)) goto oom;
        return true;
    }
    count = target->child_count;
    if (!builtin_type(ctx, NIPPY_TYPE_BOOL, &bool_type)) return false;
    for (i = count; i > 0; i--) {
        const TypedType *original = typed_program_type(ctx->program,
                                                       narrowing->original_type);
        size_t alternative, tag, payload_index, injected;
        TypedNode payload = {0};

        target = typed_program_type(ctx->program, narrowing->type_index);
        alternative = *(const size_t *)list_get(&ctx->program->type_children,
                                                target->first_child + i - 1);
        if (original == NULL ||
            !union_has_alternative(ctx->program, original, alternative, &tag)) {
            error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        payload.kind = TYPED_SUM_PAYLOAD;
        payload.type_index = alternative;
        payload.pos = pos;
        payload.name_index = TYPED_NO_NODE;
        payload.integer_value = (int)tag;
        payload.left_child = source;
        payload.right_child = TYPED_NO_NODE;
        if (!typed_program_push(ctx->program, &payload, &payload_index)) goto oom;
        if (!push_injection(ctx, payload_index, narrowing->type_index, i - 1,
                            &injected)) return false;
        if (chain == TYPED_NO_NODE) {
            chain = injected;
        } else {
            TypedNode test = {0}, select = {0};
            size_t condition, children_start;
            test.kind = TYPED_IS;
            test.type_index = bool_type;
            test.auxiliary_type = alternative;
            test.name_index = TYPED_NO_NODE;
            test.pos = pos;
            test.left_child = source;
            test.right_child = TYPED_NO_NODE;
            if (!typed_program_push(ctx->program, &test, &condition)) goto oom;
            children_start = ctx->program->children.length;
            if (!typed_program_push_child(ctx->program, chain)) goto oom;
            select.kind = TYPED_IF_EXPRESSION;
            select.type_index = narrowing->type_index;
            select.name_index = TYPED_NO_NODE;
            select.pos = pos;
            select.left_child = condition;
            select.right_child = injected;
            select.children_start = children_start;
            select.children_count = 1;
            if (!typed_program_push(ctx->program, &select, &chain)) goto oom;
        }
    }
    *out = chain;
    return true;
oom:
    error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    return false;
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

static bool push_condition_narrowing(CheckContext *ctx, size_t condition_ast, bool negated, bool allow_paths);

static bool push_condition_narrowing_left_paths(const AstNode *condition, bool negated, bool allow_paths, CheckContext *ctx, const AstNode **receiver, bool *outcome)
{
    if (condition != NULL && condition->kind == AST_BINARY &&
        ((condition->op == NIPPY_OPERATOR_AND && !negated) ||
         (condition->op == NIPPY_OPERATOR_OR && negated))) {
        bool left_paths = allow_paths &&
                          condition_is_pure(ctx->ast, condition->right_child);
        do { *outcome = (push_condition_narrowing(ctx, condition->left_child, negated,
                                        left_paths) &&
               push_condition_narrowing(ctx, condition->right_child, negated,
                                        allow_paths)); return true; } while (0);
    }
    if (condition == NULL || condition->kind != AST_IS) {
        do { *outcome = (true); return true; } while (0);
    }
    (*receiver) = ast_get_node(ctx->ast, condition->left_child);
    if ((*receiver) == NULL ||
        ((*receiver)->kind != AST_IDENTIFIER &&
         (!allow_paths || !stable_access(ctx, condition->left_child)))) {
        do { *outcome = (true); return true; } while (0);
    }
    return false;
}

static bool push_condition_narrowing_access_root(const char **name, CheckContext *ctx, const AstNode *condition, const VariableEntry **variable, const AstNode *receiver, size_t *original_type, const TypedType **declared, size_t *tested)
{
    (*name) = access_root(ctx->ast, condition->left_child);
    if ((*name) == NULL) {
        return false;
    }
    (*variable) = find_variable(ctx->locals, (*name));
    if ((*variable) == NULL && !ctx->in_function) {
        (*variable) = find_variable(ctx->globals, (*name));
    }
    if ((*variable) == NULL) {
        return false;
    }
    {
        size_t type_index = (*variable)->type_index;
        if (receiver->kind != AST_IDENTIFIER) {
            const TypedNode *value = typed_program_node(
                ctx->program, *mapping_slot(ctx->mapping, condition->left_child));
            if (value == NULL) return false;
            type_index = value->type_index;
        }
        (*original_type) = type_index;
        for (size_t i = ctx->narrowings->length; i > 0; i--) {
            const Narrowing *prior = list_get(ctx->narrowings, i - 1);
            if (prior->active && strcmp(prior->name, (*name)) == 0 &&
                ((receiver->kind == AST_IDENTIFIER &&
                  prior->path_ast == AST_NO_NODE) ||
                 (receiver->kind != AST_IDENTIFIER &&
                  prior->path_ast != AST_NO_NODE &&
                  same_access(ctx->ast, prior->path_ast, condition->left_child)))) {
                type_index = prior->type_index;
                (*original_type) = prior->original_type;
                break;
            }
        }
        (*declared) = typed_program_type(ctx->program, type_index);
        (*tested) = type_index;
    }
    return true;
}

static void push_condition_narrowing_candidate(size_t *j, List *right_facts, Narrowing *left, CheckContext *ctx, Narrowing *right, bool *found)
{
    for ((*j) = (*right_facts).length; (*j) > 0; (*j)--) {
        const Narrowing *candidate = list_get(right_facts, (*j) - 1);
        if (strcmp((*left).name, candidate->name) == 0 &&
            (*left).original_type == candidate->original_type &&
            (((*left).path_ast == AST_NO_NODE && candidate->path_ast == AST_NO_NODE) ||
             ((*left).path_ast != AST_NO_NODE && candidate->path_ast != AST_NO_NODE &&
              same_access(ctx->ast, (*left).path_ast, candidate->path_ast)))) {
            (*right) = *candidate;
            (*found) = true;
            break;
        }
    }
}

static bool push_condition_narrowing_union_has_alternative(const TypedType *declared, CheckContext *ctx, size_t *tested, size_t *tag, bool negated, bool *outcome2)
{
    if (declared == NULL ||
        !union_has_alternative(ctx->program, declared, (*tested), tag)) {
        do { *outcome2 = (true); return true; } while (0);
    }
    if (negated) {
        List alternatives;
        size_t i, first = declared->first_child, count = declared->child_count;
        list_init(&alternatives, sizeof(size_t));
        for (i = 0; i < count; i++) {
            size_t item = *(const size_t *)list_get(&ctx->program->type_children,
                                                    first + i);
            if (i != (*tag) && !list_push(&alternatives, &item)) {
                list_free(&alternatives);
                error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                do { *outcome2 = (false); return true; } while (0);
            }
        }
        if (!intern_union(ctx, &alternatives, tested)) {
            list_free(&alternatives);
            do { *outcome2 = (false); return true; } while (0);
        }
        list_free(&alternatives);
    }
    return false;
}

static void push_condition_narrowing_later(size_t *j, size_t i, List *left_facts, Narrowing *left, CheckContext *ctx, bool *duplicate)
{
    for ((*j) = i; (*j) < (*left_facts).length; (*j)++) {
        const Narrowing *later = list_get(left_facts, (*j));
        if (strcmp((*left).name, later->name) == 0 &&
            (((*left).path_ast == AST_NO_NODE && later->path_ast == AST_NO_NODE) ||
             ((*left).path_ast != AST_NO_NODE && later->path_ast != AST_NO_NODE &&
              same_access(ctx->ast, (*left).path_ast, later->path_ast)))) {
            (*duplicate) = true;
            break;
        }
    }
}

static bool push_condition_narrowing_typed_program(CheckContext *ctx, size_t tested, const TypedType *declared, size_t *tag, const char *name, const AstNode *receiver, const AstNode *condition, size_t original_type, bool *outcome3)
{
    if (typed_program_type(ctx->program, tested)->kind != NIPPY_TYPE_UNION &&
        (declared == NULL || !union_has_alternative(ctx->program, declared,
                                                    tested, tag))) do { *outcome3 = (true); return true; } while (0);
    {
        Narrowing narrowing = {0};

        narrowing.name = name;
        narrowing.path_ast = receiver->kind == AST_IDENTIFIER
                                 ? AST_NO_NODE : condition->left_child;
        narrowing.type_index = tested;
        narrowing.original_type = original_type;
        narrowing.tag = (*tag);
        narrowing.active = true;
        narrowing.alias_sensitive = receiver->kind != AST_IDENTIFIER &&
                                    access_has_list(ctx, condition->left_child);
        if (!list_push(ctx->narrowings, &narrowing)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            do { *outcome3 = (false); return true; } while (0);
        }
    }
    return false;
}

static bool push_condition_narrowing_push_condition_narrowing_access_root(const char **name, CheckContext *ctx, const AstNode *condition, const VariableEntry **variable, const AstNode *receiver, size_t *original_type, const TypedType **declared, size_t *tested, bool *outcome4)
{
    if (!push_condition_narrowing_access_root(name, ctx, condition, variable, receiver, original_type, declared, tested)) {
        do { *outcome4 = (true); return true; } while (0);
    }
    if ((*declared) == NULL || (*declared)->kind != NIPPY_TYPE_UNION) {
        do { *outcome4 = (true); return true; } while (0);
    }
    {
        size_t declared_index = (*tested);
        if (!resolve_type_in_env(ctx, condition->right_child, ctx->env, "is",
                                 tested)) {
            do { *outcome4 = (false); return true; } while (0);
        }
        (*declared) = typed_program_type(ctx->program, declared_index);
    }
    return false;
}

static void push_condition_narrowing_condition(CheckContext *ctx, size_t condition_ast, bool *negated, const AstNode **condition)
{
    (*condition) = ast_get_node(ctx->ast, condition_ast);
    
    
    
    
    
    
    
    while ((*condition) != NULL && (*condition)->kind == AST_UNARY &&
           (*condition)->op == NIPPY_OPERATOR_NOT) {
        (*negated) = !(*negated);
        (*condition) = ast_get_node(ctx->ast, (*condition)->left_child);
    }
}

static bool push_condition_narrowing(CheckContext *ctx, size_t condition_ast,
                                     bool negated, bool allow_paths)
{
    const AstNode *condition = {0};
    const AstNode *receiver = {0};
    const char *name = {0};
    const VariableEntry *variable = {0};
    size_t original_type = {0};
    const TypedType *declared = {0};
    size_t tested = {0};
    size_t tag = {0};
    push_condition_narrowing_condition(ctx, condition_ast, &negated, &condition);
    if (condition != NULL && condition->kind == AST_BINARY &&
        ((condition->op == NIPPY_OPERATOR_OR && !negated) ||
         (condition->op == NIPPY_OPERATOR_AND && negated))) {
        size_t start = ctx->narrowings->length;
        size_t i, j;
        List left_facts, right_facts;
        bool paths = allow_paths && condition_is_pure(ctx->ast, condition_ast);

        list_init(&left_facts, sizeof(Narrowing));
        list_init(&right_facts, sizeof(Narrowing));
        if (!push_condition_narrowing(ctx, condition->left_child, negated, paths))
            goto join_error;
        for (i = start; i < ctx->narrowings->length; i++) {
            const Narrowing *fact = list_get(ctx->narrowings, i);
            if (!list_push(&left_facts, fact)) goto join_oom;
        }
        ctx->narrowings->length = start;
        if (!push_condition_narrowing(ctx, condition->right_child, negated, paths))
            goto join_error;
        for (i = start; i < ctx->narrowings->length; i++) {
            const Narrowing *fact = list_get(ctx->narrowings, i);
            if (!list_push(&right_facts, fact)) goto join_oom;
        }
        ctx->narrowings->length = start;
        for (i = left_facts.length; i > 0; i--) {
            Narrowing left = *(const Narrowing *)list_get(&left_facts, i - 1);
            Narrowing right;
            List alternatives;
            size_t combined;
            bool duplicate = false, found = false;

            push_condition_narrowing_later(&j, i, &left_facts, &left, ctx, &duplicate);
            if (duplicate) continue;
            push_condition_narrowing_candidate(&j, &right_facts, &left, ctx, &right, &found);
            if (!found) continue;
            list_init(&alternatives, sizeof(size_t));
            if (!list_push(&alternatives, &left.type_index) ||
                !list_push(&alternatives, &right.type_index)) {
                list_free(&alternatives);
                goto join_oom;
            }
            if (!intern_union(ctx, &alternatives, &combined)) {
                list_free(&alternatives);
                goto join_error;
            }
            list_free(&alternatives);
            if (same_type(ctx->program, combined, left.original_type)) continue;
            left.type_index = combined;
            if (typed_program_type(ctx->program, combined)->kind != NIPPY_TYPE_UNION &&
                !union_has_alternative(ctx->program,
                     typed_program_type(ctx->program, left.original_type),
                     combined, &left.tag)) continue;
            if (!list_push(ctx->narrowings, &left)) goto join_oom;
        }
        list_free(&left_facts);
        list_free(&right_facts);
        return true;
join_oom:
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
join_error:
        ctx->narrowings->length = start;
        list_free(&left_facts);
        list_free(&right_facts);
        return false;
    }
    {
        bool outcome;

        if (push_condition_narrowing_left_paths(condition, negated, allow_paths, ctx, &receiver, &outcome)) {
            return outcome;
        }
    }
    {
        bool outcome4;

        if (push_condition_narrowing_push_condition_narrowing_access_root(&name, ctx, condition, &variable, receiver, &original_type, &declared, &tested, &outcome4)) {
            return outcome4;
        }
    }
    {
        bool outcome2;

        if (push_condition_narrowing_union_has_alternative(declared, ctx, &tested, &tag, negated, &outcome2)) {
            return outcome2;
        }
    }
    declared = typed_program_type(ctx->program, original_type);
    {
        bool outcome3;

        if (push_condition_narrowing_typed_program(ctx, tested, declared, &tag, name, receiver, condition, original_type, &outcome3)) {
            return outcome3;
        }
    }
    return true;
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


bool check_if_expression(CheckContext *ctx, const AstNode *node,
                                size_t ast_index)
{
    size_t condition;
    size_t then_expression;
    size_t else_expression;
    const TypedNode *then_node;
    const TypedNode *else_node;
    size_t narrowing_start;
    Checkpoint checkpoint;
    bool then_ok;
    size_t expected = ctx->expected_ast == ast_index
                          ? ctx->expected_type : TYPED_NO_NODE;
    TypedNode typed = {0};

    if (!check_condition(ctx, node->left_child, &condition)) {
        return false;
    }
    narrowing_start = ctx->narrowings->length;
    checkpoint = checker_checkpoint(ctx);
    if (!push_if_narrowing(ctx, node->left_child)) {
        ctx->narrowings->length = narrowing_start;
        return false;
    }
    {
        then_ok = check_expression_coerced(ctx, node->right_child, expected,
                                           &then_expression);

        ctx->narrowings->length = narrowing_start;
        if (!then_ok) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_IF_EXPR_MISMATCH);
            }
            if (ctx->errors->fatal ||
                ctx->errors->length >= ERROR_MESSAGE_CAPACITY - 1) return false;
            checker_restore(ctx, checkpoint);
            ctx->errors->reported = false;
        }
    }
    if (!push_else_narrowing(ctx, node->left_child)) return false;
    {
        bool ok = check_expression_coerced(
            ctx, *(const size_t *)list_get(&ctx->ast->children, node->children_start),
            expected, &else_expression);
        ctx->narrowings->length = narrowing_start;
        if (!ok) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_IF_EXPR_MISMATCH);
            }
            return false;
        }
    }
    if (!then_ok) {
        ctx->errors->reported = true;
        return false;
    }
    then_node = typed_program_node(ctx->program, then_expression);
    else_node = typed_program_node(ctx->program, else_expression);
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
    if (!typed_program_push_child(ctx->program, else_expression)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


static bool push_match_narrowing_value(const AstNode *head, const TypedType **declared, CheckContext *ctx, const VariableEntry *variable, size_t head_ast, Narrowing *narrowing, const char *name, size_t tested)
{
    if (head->kind == AST_IDENTIFIER) {
        (*declared) = typed_program_type(ctx->program, variable->type_index);
    } else {
        const TypedNode *value = typed_program_node(
            ctx->program, *mapping_slot(ctx->mapping, head_ast));
        (*declared) = value == NULL ? NULL :
            typed_program_type(ctx->program, value->type_index);
    }
    if ((*declared) == NULL || (*declared)->kind != NIPPY_TYPE_UNION) {
        return false;
    }
    (*narrowing).name = name;
    (*narrowing).path_ast = head->kind == AST_IDENTIFIER ? AST_NO_NODE : head_ast;
    (*narrowing).type_index = tested;
    (*narrowing).original_type = head->kind == AST_IDENTIFIER
                                  ? variable->type_index
                                  : typed_program_node(ctx->program,
                                        *mapping_slot(ctx->mapping, head_ast))->type_index;
    for (size_t i = ctx->narrowings->length; i > 0; i--) {
        const Narrowing *prior = list_get(ctx->narrowings, i - 1);
        if (prior->active && strcmp(prior->name, name) == 0 &&
            ((head->kind == AST_IDENTIFIER && prior->path_ast == AST_NO_NODE) ||
             (head->kind != AST_IDENTIFIER && prior->path_ast != AST_NO_NODE &&
              same_access(ctx->ast, prior->path_ast, head_ast)))) {
            (*narrowing).original_type = prior->original_type;
            break;
        }
    }
    return true;
}

bool push_match_narrowing(CheckContext *ctx, size_t head_ast,
                                  size_t tested, size_t tag)
{
    const AstNode *head = ast_get_node(ctx->ast, head_ast);
    const char *name;
    const VariableEntry *variable;
    const TypedType *declared;
    Narrowing narrowing = {0};

    if (head == NULL ||
        (head->kind != AST_IDENTIFIER && !stable_access(ctx, head_ast))) {
        return true;
    }
    name = access_root(ctx->ast, head_ast);
    if (name == NULL) {
        return true;
    }
    variable = find_variable(ctx->locals, name);
    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL) {
        return true;
    }
    if (!push_match_narrowing_value(head, &declared, ctx, variable, head_ast, &narrowing, name, tested)) {
        return true;
    }
    declared = typed_program_type(ctx->program, narrowing.original_type);
    if (declared == NULL ||
        !union_has_alternative(ctx->program, declared, tested, &tag)) return true;
    narrowing.tag = tag;
    narrowing.active = true;
    narrowing.alias_sensitive = head->kind != AST_IDENTIFIER &&
                                access_has_list(ctx, head_ast);
    if (!list_push(ctx->narrowings, &narrowing)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    return true;
}


static bool check_match_else(bool has_else, bool expression_form, CheckContext *ctx, const AstNode *node, size_t ast_index, size_t *else_typed, List *conditions, List *bodies, List *body_types, size_t *else_type, bool had_arm_errors, size_t return_type, bool exhaustive)
{
    if (has_else) {
        if (expression_form) {
            const TypedNode *else_node;

            if (!check_expression_coerced(
                    ctx, node->right_child,
                    ctx->expected_ast == ast_index ? ctx->expected_type
                                                   : TYPED_NO_NODE,
                    else_typed)) {
                if (!ctx->errors->reported) {
                    error_report_at(ctx->errors, node->pos,
                                    MESSAGE_CHECKER_IF_EXPR_MISMATCH);
                }
                list_free(conditions);
                list_free(bodies);
                list_free(body_types);
                return false;
            }
            else_node = typed_program_node(ctx->program, (*else_typed));
            (*else_type) = else_node == NULL ? TYPED_NO_NODE
                                          : else_node->type_index;
            if (had_arm_errors) {
                ctx->errors->reported = true;
                list_free(conditions); list_free(bodies); list_free(body_types);
                return false;
            }
            if ((*else_type) == TYPED_NO_NODE ||
                ((*body_types).length > 0 &&
                 !same_type(ctx->program,
                            *(const size_t *)list_get(body_types, 0),
                            (*else_type)))) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_IF_EXPR_MISMATCH);
                list_free(conditions);
                list_free(bodies);
                list_free(body_types);
                return false;
            }
        } else if (!check_block(ctx, node->right_child, return_type,
                                else_typed)) {
            list_free(conditions);
            list_free(bodies);
            list_free(body_types);
            return false;
        }
    }
    if (had_arm_errors) {
        ctx->errors->reported = true;
        list_free(conditions); list_free(bodies); list_free(body_types);
        return false;
    }
    if (!exhaustive) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
        list_free(conditions);
        list_free(bodies);
        list_free(body_types);
        return false;
    }
    return true;
}

static bool check_match_nippy_type_is_numeric(bool *comparable, const TypedType *pattern_type, const TypedType *head_type, CheckContext *ctx, size_t head_type_index, const TypedNode *pattern_node, const AstNode *node, TypedNode *test, size_t bool_type, size_t head, size_t pattern, size_t *condition)
{
    (*comparable) = pattern_type != NULL && head_type != NULL &&
        ((nippy_type_is_numeric(head_type->kind) &&
          nippy_type_is_numeric(pattern_type->kind)) ||
         ((head_type->kind == NIPPY_TYPE_STRING ||
           head_type->kind == NIPPY_TYPE_DYNAMIC_STRING) &&
          (pattern_type->kind == NIPPY_TYPE_STRING ||
           pattern_type->kind == NIPPY_TYPE_DYNAMIC_STRING)) ||
         (same_type(ctx->program, head_type_index, pattern_node->type_index) &&
          (head_type->kind == NIPPY_TYPE_NIL ||
           equality_type_supported(ctx->program, head_type_index, 64))));
    if (!(*comparable)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MATCH_PATTERN);
        return true;
    }
    (*test).kind = TYPED_BINARY;
    (*test).op = NIPPY_OPERATOR_EQ;
    (*test).type_index = bool_type;
    (*test).left_child = head;
    (*test).right_child = pattern;
    (*test).pos = node->pos;
    if (!typed_program_push(ctx->program, test, condition)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return true;
    }
    return false;
}

static int check_match_body(bool expression_form, CheckContext *ctx, size_t body_ast, size_t ast_index, size_t *body, const AstNode *node, size_t *body_type, List *body_types, size_t return_type, size_t narrowing_start, List *conditions, size_t *condition, List *bodies)
{
    if (expression_form) {
        const TypedNode *body_node;

        if (!check_expression_coerced(
                ctx, body_ast,
                ctx->expected_ast == ast_index ? ctx->expected_type
                                               : TYPED_NO_NODE,
                body)) {
            if (!ctx->errors->reported) {
                error_report_at(ctx->errors, node->pos,
                                MESSAGE_CHECKER_IF_EXPR_MISMATCH);
            }
            return 1;
        }
        body_node = typed_program_node(ctx->program, (*body));
        (*body_type) = body_node == NULL ? TYPED_NO_NODE
                                      : body_node->type_index;
        if ((*body_type) == TYPED_NO_NODE ||
            ((*body_types).length > 0 &&
             !same_type(ctx->program,
                        *(const size_t *)list_get(body_types, 0),
                        (*body_type)))) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_IF_EXPR_MISMATCH);
            return 1;
        }
    } else if (!check_block(ctx, body_ast, return_type, body)) {
        return 1;
    } else {
        (*body_type) = TYPED_NO_NODE;
    }
    ctx->narrowings->length = narrowing_start;
    if (!list_push(conditions, condition) ||
        !list_push(bodies, body) ||
        !list_push(body_types, body_type)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        list_free(conditions);
        list_free(bodies);
        list_free(body_types);
        return 2;
    }
    return 0;
}

static bool check_match_head_is_union(CheckContext *ctx, size_t pattern_ast, size_t *tested, const TypedType **head_type, size_t head_type_index, size_t *tag, const AstNode *node, size_t *j, List *conditions, TypedNode *test, size_t bool_type, size_t head, size_t *condition)
{
    bool head_is_union;

    if (!resolve_type_in_env(ctx, pattern_ast, ctx->env, "match",
                             tested)) {
        return true;
    }
    /* Resolving the pattern can add a type and move the type array. */
    (*head_type) = typed_program_type(ctx->program, head_type_index);
    head_is_union = (*head_type) != NULL &&
                    (*head_type)->kind == NIPPY_TYPE_UNION;
    if (head_is_union) {
        if (!union_has_alternative(ctx->program, (*head_type), (*tested),
                                   tag)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MATCH_PATTERN);
            return true;
        }
    } else if (!same_type(ctx->program, head_type_index, (*tested))) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MATCH_PATTERN);
        return true;
    }
    for ((*j) = 0; (*j) < (*conditions).length; (*j)++) {
        size_t previous = *(const size_t *)list_get(conditions, (*j));
        const TypedNode *previous_test = typed_program_node(ctx->program,
                                                            previous);
        if (previous_test != NULL &&
            same_type(ctx->program, previous_test->auxiliary_type, (*tested))) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_MATCH_DUPLICATE);
            return true;
        }
    }
    (*test).kind = TYPED_IS;
    (*test).type_index = bool_type;
    (*test).auxiliary_type = (*tested);
    (*test).left_child = head;
    (*test).integer_value = head_is_union ? (int)(*tag) : 1;
    (*test).pos = node->pos;
    if (!typed_program_push(ctx->program, test, condition)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return true;
    }
    if (!push_match_narrowing(ctx, node->left_child, (*tested), (*tag))) {
        return true;
    }
    return false;
}

static bool check_match_builtin(const TypedType *head_type, const TypedNode **pattern_node, CheckContext *ctx, size_t *target, size_t *pattern, size_t *coerced, const TypedType **pattern_type)
{
    if (head_type == NULL ||
        (head_type->kind != NIPPY_TYPE_STRING &&
         head_type->kind != NIPPY_TYPE_DYNAMIC_STRING) ||
        (head_type->kind == NIPPY_TYPE_STRING &&
         (*pattern_node)->string_length > head_type->fixed_length)) {
        if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, target)) {
            return true;
        }
    }
    if (!inject_into(ctx, (*pattern), (*target), coerced)) {
        return true;
    }
    (*pattern) = (*coerced);
    (*pattern_node) = typed_program_node(ctx->program, (*pattern));
    (*pattern_type) = (*pattern_node) == NULL ? NULL :
        typed_program_type(ctx->program, (*pattern_node)->type_index);
    return false;
}

static bool check_match_step(size_t *head_type_index, const TypedNode *head_node, const TypedType **head_type, CheckContext *ctx, size_t *bool_type, size_t *nil_type, const AstNode *node, bool type_match)
{
    (*head_type_index) = head_node == NULL ? TYPED_NO_NODE : head_node->type_index;
    if ((*head_type) == NULL ||
        !builtin_type(ctx, NIPPY_TYPE_BOOL, bool_type) ||
        !builtin_type(ctx, NIPPY_TYPE_NIL, nil_type)) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        return false;
    }
    (*head_type) = typed_program_type(ctx->program, (*head_type_index));
    if (!type_match && (*head_type) != NULL && (*head_type)->kind != NIPPY_TYPE_NIL &&
        !equality_type_supported(ctx->program, (*head_type_index), NIPPY_TYPE_DEPTH)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_MATCH_PATTERN);
        return false;
    }
    return true;
}

static void check_match_seen_true(size_t *i, const AstNode *node, CheckContext *ctx, bool *exhaustive)
{
    bool seen_true = false;
    bool seen_false = false;

    for ((*i) = 0; (*i) < node->children_count; (*i) += 2) {
        size_t pattern_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + (*i));
        const AstNode *pattern = ast_get_node(ctx->ast, pattern_ast);

        if (pattern != NULL && pattern->kind == AST_BOOL_LITERAL) {
            if (pattern->integer_value != 0) {
                seen_true = true;
            } else {
                seen_false = true;
            }
        }
    }
    (*exhaustive) = seen_true && seen_false;
}

static void check_match_alternative(bool type_match, const TypedType *head_type, bool *exhaustive, size_t *i, CheckContext *ctx, const AstNode *node, size_t head_type_index, size_t bool_type, size_t nil_type, List *body_types)
{
    if (type_match && head_type->kind == NIPPY_TYPE_UNION) {
        (*exhaustive) = true;
        for ((*i) = 0; (*i) < head_type->child_count; (*i)++) {
            size_t alternative = *(const size_t *)list_get(
                &ctx->program->type_children,
                head_type->first_child + (*i));
            size_t j;
            bool found = false;

            for (j = 0; j < node->children_count; j += 2) {
                size_t pattern_ast = *(const size_t *)list_get(
                    &ctx->ast->children, node->children_start + j);
                size_t pattern_type;

                if (resolve_type_in_env(ctx, pattern_ast, ctx->env,
                                        "match", &pattern_type) &&
                    same_type(ctx->program, alternative, pattern_type)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                (*exhaustive) = false;
                break;
            }
        }
    } else if (!type_match &&
               same_type(ctx->program, head_type_index,
                         bool_type)) {
        check_match_seen_true(i, node, ctx, exhaustive);
    } else if (!type_match &&
               same_type(ctx->program, head_type_index,
                         nil_type)) {
        (*exhaustive) = true;
    } else if (type_match && head_type->kind != NIPPY_TYPE_UNION) {
        (*exhaustive) = (*body_types).length > 0;
    }
}

static bool check_match_step11(size_t *chain, CheckContext *ctx, const AstNode *node, size_t temporary_index, bool expression_form, size_t original_head)
{
    if ((*chain) == TYPED_NO_NODE) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
        return false;
    }
    if (temporary_index != TYPED_NO_NODE) {
        TypedNode bind = {0};
        const TypedNode *result = typed_program_node(ctx->program, (*chain));

        bind.kind = expression_form ? TYPED_MATCH_BIND_EXPRESSION
                                    : TYPED_MATCH_BIND_STATEMENT;
        bind.type_index = expression_form ? result->type_index : TYPED_NO_NODE;
        bind.pos = node->pos;
        bind.name_index = TYPED_NO_NODE;
        bind.left_child = original_head;
        bind.right_child = (*chain);
        bind.integer_value_u = temporary_index;
        if (!typed_program_push(ctx->program, &bind, chain)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            return false;
        }
    }
    return true;
}

static bool check_match_scrutinee(CheckContext *ctx, const AstNode *node, size_t head_type_index, size_t *temporary_index, size_t *original_head, size_t *head)
{
    {
        const AstNode *scrutinee = ast_get_node(ctx->ast, node->left_child);
        if (scrutinee != NULL) {
            size_t *scope = (size_t *)list_get(&ctx->program->match_scopes,
                                                ctx->match_scope);
            TypedMatchTemporary temporary;
            TypedNode reference = {0};

            if (scope == NULL) {
                error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
            temporary.type_index = head_type_index;
            temporary.next = *scope;
            (*temporary_index) = ctx->program->match_temporaries.length;
            if (!list_push(&ctx->program->match_temporaries, &temporary)) {
                error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
            *scope = (*temporary_index);
            (*original_head) = (*head);
            reference.kind = TYPED_MATCH_TEMPORARY;
            reference.type_index = head_type_index;
            reference.pos = scrutinee->pos;
            reference.name_index = TYPED_NO_NODE;
            reference.left_child = TYPED_NO_NODE;
            reference.right_child = TYPED_NO_NODE;
            reference.integer_value_u = (*temporary_index);
            if (!typed_program_push(ctx->program, &reference, head)) {
                error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                return false;
            }
        }
    }
    return true;
}

static void check_match_step14(size_t *i, List *bodies, bool expression_form, bool has_else, bool exhaustive, size_t *chain)
{
    (*i) = (*bodies).length;
    if (!expression_form && !has_else && exhaustive && (*i) > 0) {
        (*chain) = *(const size_t *)list_get(bodies, --(*i));
    }
}

static bool check_match_step16(size_t *children_start, CheckContext *ctx, size_t body, size_t chain, List *conditions, List *bodies, List *body_types, TypedNode *lowered)
{
    (*children_start) = ctx->program->children.length;
    if (!typed_program_push_child(ctx->program, body) ||
        (chain != TYPED_NO_NODE &&
         !typed_program_push_child(ctx->program, chain))) {
        error_report_fatal(ctx->errors,
                     MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        list_free(conditions);
        list_free(bodies);
        list_free(body_types);
        return false;
    }
    (*lowered).kind = TYPED_IF;
    (*lowered).type_index = TYPED_NO_NODE;
    (*lowered).children_start = (*children_start);
    (*lowered).children_count =
        chain == TYPED_NO_NODE ? 1 : 2;
    return true;
}

static bool check_match_pattern(CheckContext *ctx, size_t pattern_ast, size_t *pattern, const TypedType **pattern_type, const TypedNode **pattern_node)
{
    if (!check_expression(ctx, pattern_ast, pattern) ||
        !check_default_numeric(ctx, (*pattern))) {
        return true;
    }
    (*pattern_node) = typed_program_node(ctx->program, (*pattern));
    (*pattern_type) = (*pattern_node) == NULL ? NULL :
        typed_program_type(ctx->program, (*pattern_node)->type_index);
    return false;
}

static bool check_match_error_unknown_if_none(CheckContext *ctx, List *conditions, List *bodies, List *body_types)
{
    if (!ctx->errors->reported) error_unknown_if_none(ctx->errors);
    if (ctx->errors->fatal ||
        ctx->errors->length >= ERROR_MESSAGE_CAPACITY - 1) {
        list_free(conditions);
        list_free(bodies);
        list_free(body_types);
        return false;
    }
    return true;
}

static bool check_match_had_arm_errors(CheckContext *ctx, const AstNode *node, size_t *head, const TypedNode **head_node, const TypedType **head_type, size_t *temporary_index, size_t *original_head, bool *had_arm_errors, size_t *chain)
{
    (*had_arm_errors) = false;
    
    
    
    
    (*chain) = TYPED_NO_NODE;
    (*original_head) = TYPED_NO_NODE;
    (*temporary_index) = TYPED_NO_NODE;

    if (!check_expression(ctx, node->left_child, head) ||
        !check_default_numeric(ctx, (*head))) {
        return false;
    }
    (*head_node) = typed_program_node(ctx->program, (*head));
    (*head_type) = (*head_node) == NULL
                    ? NULL
                    : typed_program_type(ctx->program, (*head_node)->type_index);
    return true;
}

static bool check_match_builtin21(CheckContext *ctx, size_t *text_type, size_t *head, size_t *coerced, const TypedNode **head_node, const TypedType **head_type)
{
    if (!builtin_type(ctx, NIPPY_TYPE_DYNAMIC_STRING, text_type) ||
        !inject_into(ctx, (*head), (*text_type), coerced)) return false;
    (*head) = (*coerced);
    (*head_node) = typed_program_node(ctx->program, (*head));
    (*head_type) = (*head_node) == NULL ? NULL :
        typed_program_type(ctx->program, (*head_node)->type_index);
    return true;
}

bool check_match(CheckContext *ctx, const AstNode *node,
                        size_t ast_index, bool expression_form,
                        size_t return_type, size_t *out_typed)
{
    bool type_match = node->integer_value != 0;
    size_t head;
    const TypedNode *head_node;
    const TypedType *head_type;
    /* The scrutinee's type never changes while the arms are checked, but the
       pointers to it do move as nodes and types are added, so arms read this
       index and fetch the pointers again where they need them. */
    size_t head_type_index;
    size_t bool_type;
    size_t nil_type;
    size_t else_typed = TYPED_NO_NODE;
    size_t else_type = TYPED_NO_NODE;
    bool has_else = node->right_child != AST_NO_NODE;
    bool exhaustive = has_else;
    size_t temporary_index = {0};
    size_t original_head = {0};
    List conditions = {0};
    List bodies = {0};
    List body_types = {0};
    size_t i = {0};
    bool had_arm_errors = {0};
    size_t chain = {0};
    if (!check_match_had_arm_errors(ctx, node, &head, &head_node, &head_type, &temporary_index, &original_head, &had_arm_errors, &chain)) {
        return false;
    }
    if (head_type != NULL && head_type->kind == NIPPY_TYPE_STRING_LITERAL) {
        size_t text_type;
        size_t coerced;
        if (!check_match_builtin21(ctx, &text_type, &head, &coerced, &head_node, &head_type)) {
        return false;
    }
    }
    if (!check_match_step(&head_type_index, head_node, &head_type, ctx, &bool_type, &nil_type, node, type_match)) {
        return false;
    }
    if (!check_match_scrutinee(ctx, node, head_type_index, &temporary_index, &original_head, &head)) {
        return false;
    }
    list_init(&conditions, sizeof(size_t));
    list_init(&bodies, sizeof(size_t));
    list_init(&body_types, sizeof(size_t));
    for (i = 0; i < node->children_count; i += 2) {
        size_t pattern_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i);
        size_t body_ast = *(const size_t *)list_get(
            &ctx->ast->children, node->children_start + i + 1);
        size_t condition;
        head_node = typed_program_node(ctx->program, head);
        head_type = head_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             head_node->type_index);
        if (head_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
        size_t body;
        size_t body_type;
        size_t narrowing_start = ctx->narrowings->length;
        Checkpoint checkpoint;
        TypedNode test = {0};

        test.name_index = TYPED_NO_NODE;
        checkpoint = checker_checkpoint(ctx);

        if (type_match) {
            size_t tested;
            size_t tag = 0;
            size_t j;
            if (check_match_head_is_union(ctx, pattern_ast, &tested, &head_type, head_type_index, &tag, node, &j, &conditions, &test, bool_type, head, &condition)) {
        goto arm_error;
    }
        } else {
            size_t pattern;
            const TypedType *pattern_type = {0};
            const TypedNode *pattern_node = {0};
            bool comparable = {0};
            if (check_match_pattern(ctx, pattern_ast, &pattern, &pattern_type, &pattern_node)) {
        goto arm_error;
    }
            head_type = typed_program_type(ctx->program, head_type_index);
            if (pattern_type != NULL &&
                pattern_type->kind == NIPPY_TYPE_STRING_LITERAL) {
                size_t target = head_type_index;
                size_t coerced;
                if (check_match_builtin(head_type, &pattern_node, ctx, &target, &pattern, &coerced, &pattern_type)) {
        goto arm_error;
    }
                head_type = typed_program_type(ctx->program, head_type_index);
            }
            if (check_match_nippy_type_is_numeric(&comparable, pattern_type, head_type, ctx, head_type_index, pattern_node, node, &test, bool_type, head, pattern, &condition)) {
        goto arm_error;
    }
        }
        {
        int status = check_match_body(expression_form, ctx, body_ast, ast_index, &body, node, &body_type, &body_types, return_type, narrowing_start, &conditions, &condition, &bodies);

        if (status == 1) {
            goto arm_error;
        }
        if (status == 2) {
            return false;
        }
    }
        continue;

arm_error:
        ctx->narrowings->length = narrowing_start;
        if (!check_match_error_unknown_if_none(ctx, &conditions, &bodies, &body_types)) {
        return false;
    }
        checker_restore(ctx, checkpoint);
        had_arm_errors = true;
        ctx->errors->reported = false;
    }
    if (had_arm_errors && !has_else) {
        ctx->errors->reported = true;
        list_free(&conditions);
        list_free(&bodies);
        list_free(&body_types);
        return false;
    }
    if (!has_else) {
        head_node = typed_program_node(ctx->program, head);
        head_type = head_node == NULL
                        ? NULL
                        : typed_program_type(ctx->program,
                                             head_node->type_index);
        if (head_type == NULL) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
        check_match_alternative(type_match, head_type, &exhaustive, &i, ctx, node, head_type_index, bool_type, nil_type, &body_types);
    }
    if (!check_match_else(has_else, expression_form, ctx, node, ast_index, &else_typed, &conditions, &bodies, &body_types, &else_type, had_arm_errors, return_type, exhaustive)) {
        return false;
    }
    chain = else_typed;
    check_match_step14(&i, &bodies, expression_form, has_else, exhaustive, &chain);
    for (; i > 0; i--) {
        size_t condition = *(const size_t *)list_get(&conditions, i - 1);
        size_t body = *(const size_t *)list_get(&bodies, i - 1);
        TypedNode lowered = {0};

        lowered.name_index = TYPED_NO_NODE;
        lowered.pos = node->pos;
        lowered.left_child = condition;
        if (expression_form) {
            size_t children_start = ctx->program->children.length;

            if (chain == TYPED_NO_NODE ||
                !typed_program_push_child(ctx->program, chain)) {
                error_report_fatal(ctx->errors,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                list_free(&conditions);
                list_free(&bodies);
                list_free(&body_types);
                return false;
            }
            lowered.kind = TYPED_IF_EXPRESSION;
            lowered.type_index = *(const size_t *)list_get(&body_types, i - 1);
            lowered.right_child = body;
            lowered.children_start = children_start;
            lowered.children_count = 1;
        } else {
            size_t children_start;

            if (chain != TYPED_NO_NODE) {
                const TypedNode *chain_node =
                    typed_program_node(ctx->program, chain);

                if (chain_node != NULL && chain_node->kind != TYPED_BLOCK) {
                    TypedNode wrapper = {0};
                    size_t wrapper_start = ctx->program->children.length;
                    size_t wrapped;

                    if (!typed_program_push_child(ctx->program, chain)) {
                        error_report_fatal(ctx->errors,
                                     MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                        list_free(&conditions);
                        list_free(&bodies);
                        list_free(&body_types);
                        return false;
                    }
                    wrapper.kind = TYPED_BLOCK;
                    wrapper.name_index = TYPED_NO_NODE;
                    wrapper.type_index = TYPED_NO_NODE;
                    wrapper.pos = node->pos;
                    wrapper.children_start = wrapper_start;
                    wrapper.children_count = 1;
                    if (!typed_program_push(ctx->program, &wrapper, &wrapped)) {
                        error_report_fatal(ctx->errors,
                                     MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
                        list_free(&conditions);
                        list_free(&bodies);
                        list_free(&body_types);
                        return false;
                    }
                    chain = wrapped;
                }
            }
            if (!check_match_step16(&children_start, ctx, body, chain, &conditions, &bodies, &body_types, &lowered)) {
        return false;
    }
        }
        if (!typed_program_push(ctx->program, &lowered, &chain)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
            list_free(&conditions);
            list_free(&bodies);
            list_free(&body_types);
            return false;
        }
    }
    list_free(&conditions);
    list_free(&bodies);
    list_free(&body_types);
    if (!check_match_step11(&chain, ctx, node, temporary_index, expression_form, original_head)) {
        return false;
    }
    *mapping_slot(ctx->mapping, ast_index) = chain;
    *out_typed = chain;
    return true;
}
