#include "checker_internal.h"

/* match, as a statement and as an expression.

   A match is lowered to a chain of ifs: each arm becomes `if condition then
   body else <the rest>`. A match on a type tests which alternative a union
   holds; a match on a value compares for equality. The scrutinee is evaluated
   once, into a temporary the conditions read.

   The work is four phases over one Match: check the head, check each arm,
   decide whether the arms are complete, then lower. */

typedef struct {
    CheckContext *ctx;
    const AstNode *node;
    size_t ast_index;
    bool expression_form;
    bool type_match;
    size_t return_type;
    bool has_else;
    bool exhaustive;
    bool had_arm_errors;
    /* The scrutinee. Its type never changes while the arms are checked, but
       the pointers to it do move as nodes and types are added, so arms read
       the index and fetch the pointers again where they need them. */
    size_t head;
    size_t head_type_index;
    size_t bool_type;
    size_t nil_type;
    size_t original_head;   /* the expression the temporary was bound to */
    size_t temporary_index; /* TYPED_NO_NODE when the head needs no binding */
    List conditions;        /* per arm, in source order */
    List bodies;
    List body_types;
    size_t else_typed;
    size_t chain;           /* the lowered statement or expression */
} Match;

typedef enum {
    ARM_OK,
    ARM_STOP
} ArmResult;

static void match_open(Match *m, CheckContext *ctx, const AstNode *node,
                       size_t ast_index, bool expression_form,
                       size_t return_type)
{
    bool has_else = node->right_child != AST_NO_NODE;

    *m = (Match){
        .ctx = ctx,
        .node = node,
        .ast_index = ast_index,
        .expression_form = expression_form,
        .type_match = node->integer_value != 0,
        .return_type = return_type,
        .has_else = has_else,
        .exhaustive = has_else,
        .original_head = TYPED_NO_NODE,
        .temporary_index = TYPED_NO_NODE,
        .else_typed = TYPED_NO_NODE,
        .chain = TYPED_NO_NODE,
    };
    list_init(&m->conditions, sizeof(size_t));
    list_init(&m->bodies, sizeof(size_t));
    list_init(&m->body_types, sizeof(size_t));
}

static void match_close(Match *m)
{
    list_free(&m->conditions);
    list_free(&m->bodies);
    list_free(&m->body_types);
}

static const TypedType *match_head_type(const Match *m)
{
    const TypedNode *head = typed_program_node(m->ctx->program, m->head);

    return head == NULL ? NULL
                        : typed_program_type(m->ctx->program, head->type_index);
}

static size_t match_child(const Match *m, size_t i)
{
    return *(const size_t *)list_get(&m->ctx->ast->children,
                                     m->node->children_start + i);
}

static size_t match_expected(const Match *m)
{
    return m->ctx->expected_ast == m->ast_index ? m->ctx->expected_type
                                                : TYPED_NO_NODE;
}

static bool match_unsupported(const Match *m)
{
    error_report_at(m->ctx->errors, m->node->pos,
                    MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
    return false;
}

/* ---- the head ---- */

/* The scrutinee is held in a temporary so every arm reads the one value. */
static bool match_bind_scrutinee(Match *m)
{
    CheckContext *ctx = m->ctx;
    const AstNode *scrutinee = ast_get_node(ctx->ast, m->node->left_child);
    size_t *scope = (size_t *)list_get(&ctx->program->match_scopes,
                                       ctx->match_scope);
    TypedMatchTemporary temporary;
    TypedNode reference = {0};

    if (scrutinee == NULL) {
        return true;
    }
    if (scope == NULL) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    temporary.type_index = m->head_type_index;
    temporary.next = *scope;
    m->temporary_index = ctx->program->match_temporaries.length;
    checker_ok(ctx->errors,
               list_push(&ctx->program->match_temporaries, &temporary),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    *scope = m->temporary_index;
    m->original_head = m->head;
    reference.kind = TYPED_MATCH_TEMPORARY;
    reference.type_index = m->head_type_index;
    reference.pos = scrutinee->pos;
    reference.name_index = TYPED_NO_NODE;
    reference.left_child = TYPED_NO_NODE;
    reference.right_child = TYPED_NO_NODE;
    reference.integer_value_u = m->temporary_index;
    return checker_push_node(ctx, &reference, &m->head,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* A string literal head is a Text, so arms compare against a Text. */
static bool match_coerce_text_head(Match *m)
{
    const TypedType *type = match_head_type(m);
    size_t text_type;
    size_t coerced;

    if (type == NULL || type->kind != NYX_TYPE_STRING_LITERAL) {
        return true;
    }
    if (!builtin_type(m->ctx, NYX_TYPE_DYNAMIC_STRING, &text_type) ||
        !inject_into(m->ctx, m->head, text_type, &coerced)) {
        return false;
    }
    m->head = coerced;
    return true;
}

/* Whether the head can be matched at all: a value match compares for
   equality, so the head's type must support it. */
static bool match_head_supported(const Match *m)
{
    const TypedType *type =
        typed_program_type(m->ctx->program, m->head_type_index);

    if (m->type_match || type == NULL || type->kind == NYX_TYPE_NIL ||
        equality_type_supported(m->ctx->program, m->head_type_index,
                                NYX_TYPE_DEPTH)) {
        return true;
    }
    error_report_at(m->ctx->errors, m->node->pos, MESSAGE_CHECKER_MATCH_PATTERN);
    return false;
}

static bool match_check_head(Match *m)
{
    CheckContext *ctx = m->ctx;
    const TypedNode *head_node;

    if (!check_expression(ctx, m->node->left_child, &m->head) ||
        !check_default_numeric(ctx, m->head) || !match_coerce_text_head(m)) {
        return false;
    }
    head_node = typed_program_node(ctx->program, m->head);
    m->head_type_index = head_node == NULL ? TYPED_NO_NODE
                                           : head_node->type_index;
    if (match_head_type(m) == NULL ||
        !builtin_type(ctx, NYX_TYPE_BOOL, &m->bool_type) ||
        !builtin_type(ctx, NYX_TYPE_NIL, &m->nil_type)) {
        return match_unsupported(m);
    }
    return match_head_supported(m) && match_bind_scrutinee(m);
}

/* ---- arms ---- */

static bool is_text_kind(NyxType kind)
{
    return kind == NYX_TYPE_STRING || kind == NYX_TYPE_DYNAMIC_STRING;
}

/* Whether a pattern can be compared with the head: two numbers, two texts, or
   two values of one type that supports equality. */
static bool match_comparable(const Match *m, const TypedType *head_type,
                             const TypedNode *pattern_node,
                             const TypedType *pattern_type)
{
    const TypedProgram *program = m->ctx->program;

    if (pattern_type == NULL || head_type == NULL) {
        return false;
    }
    return (nyx_type_is_numeric(head_type->kind) &&
            nyx_type_is_numeric(pattern_type->kind)) ||
           (is_text_kind(head_type->kind) && is_text_kind(pattern_type->kind)) ||
           (same_type(program, m->head_type_index, pattern_node->type_index) &&
            (head_type->kind == NYX_TYPE_NIL ||
             equality_type_supported(program, m->head_type_index,
                                     NYX_TYPE_DEPTH)));
}

/* A string literal pattern takes the head's string type when it fits there,
   and is a Text otherwise. */
static bool match_coerce_text_pattern(Match *m, size_t *pattern,
                                      const TypedNode **node,
                                      const TypedType **type)
{
    CheckContext *ctx = m->ctx;
    const TypedType *head_type = typed_program_type(ctx->program,
                                                    m->head_type_index);
    size_t target = m->head_type_index;
    size_t coerced;
    bool fits = head_type != NULL && is_text_kind(head_type->kind) &&
                !(head_type->kind == NYX_TYPE_STRING &&
                  (*node)->string_length > head_type->fixed_length);

    if (!fits && !builtin_type(ctx, NYX_TYPE_DYNAMIC_STRING, &target)) {
        return false;
    }
    if (!inject_into(ctx, *pattern, target, &coerced)) {
        return false;
    }
    *pattern = coerced;
    *node = typed_program_node(ctx->program, coerced);
    *type = *node == NULL ? NULL
                          : typed_program_type(ctx->program, (*node)->type_index);
    return true;
}

/* The condition of an arm that matches a value: head == pattern. */
static bool match_value_condition(Match *m, size_t pattern_ast,
                                  size_t *condition)
{
    CheckContext *ctx = m->ctx;
    const TypedNode *pattern_node;
    const TypedType *pattern_type;
    TypedNode test = {0};
    size_t pattern;

    if (!check_expression(ctx, pattern_ast, &pattern) ||
        !check_default_numeric(ctx, pattern)) {
        return false;
    }
    pattern_node = typed_program_node(ctx->program, pattern);
    pattern_type = pattern_node == NULL
                       ? NULL
                       : typed_program_type(ctx->program,
                                            pattern_node->type_index);
    if (pattern_type != NULL && pattern_type->kind == NYX_TYPE_STRING_LITERAL &&
        !match_coerce_text_pattern(m, &pattern, &pattern_node, &pattern_type)) {
        return false;
    }
    if (!match_comparable(m, typed_program_type(ctx->program, m->head_type_index),
                          pattern_node, pattern_type)) {
        error_report_at(ctx->errors, m->node->pos,
                        MESSAGE_CHECKER_MATCH_PATTERN);
        return false;
    }
    test.name_index = TYPED_NO_NODE;
    test.kind = TYPED_BINARY;
    test.op = NYX_OPERATOR_EQ;
    test.type_index = m->bool_type;
    test.left_child = m->head;
    test.right_child = pattern;
    test.pos = m->node->pos;
    return checker_push_node(ctx, &test, condition,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* A type pattern must be an alternative of the head's union (or the head's
   own type), and no two arms may test the same one. */
static bool match_type_is_new(const Match *m, size_t tested)
{
    CheckContext *ctx = m->ctx;
    size_t j;

    for (j = 0; j < m->conditions.length; j++) {
        const TypedNode *previous = typed_program_node(
            ctx->program, *(const size_t *)list_get(&m->conditions, j));

        if (previous != NULL &&
            same_type(ctx->program, previous->auxiliary_type, tested)) {
            error_report_at(ctx->errors, m->node->pos,
                            MESSAGE_CHECKER_MATCH_DUPLICATE);
            return false;
        }
    }
    return true;
}

/* The condition of an arm that matches a type: head is T. */
static bool match_type_condition(Match *m, size_t pattern_ast,
                                 size_t *condition)
{
    CheckContext *ctx = m->ctx;
    const TypedType *head_type;
    TypedNode test = {0};
    size_t tested;
    size_t tag = 0;
    bool head_is_union;

    if (!resolve_type_in_env(ctx, pattern_ast, ctx->env, "match", &tested)) {
        return false;
    }
    /* Resolving the pattern can add a type and move the type array. */
    head_type = typed_program_type(ctx->program, m->head_type_index);
    head_is_union = head_type != NULL && head_type->kind == NYX_TYPE_UNION;
    if (head_is_union ? !union_has_alternative(ctx->program, head_type, tested,
                                              &tag)
                      : !same_type(ctx->program, m->head_type_index, tested)) {
        error_report_at(ctx->errors, m->node->pos,
                        MESSAGE_CHECKER_MATCH_PATTERN);
        return false;
    }
    if (!match_type_is_new(m, tested)) {
        return false;
    }
    test.name_index = TYPED_NO_NODE;
    test.kind = TYPED_IS;
    test.type_index = m->bool_type;
    test.auxiliary_type = tested;
    test.left_child = m->head;
    test.integer_value = head_is_union ? (int)tag : 1;
    test.pos = m->node->pos;
    return checker_push_node(ctx, &test, condition,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING) &&
           push_match_narrowing(ctx, m->node->left_child, tested, tag);
}

/* Every arm of a match expression has the type of the first. */
static bool match_type_agrees(const Match *m, size_t type)
{
    return type != TYPED_NO_NODE &&
           (m->body_types.length == 0 ||
            same_type(m->ctx->program,
                      *(const size_t *)list_get(&m->body_types, 0), type));
}

/* A match expression's arm: a coerced expression whose type is recorded. */
static bool match_expression_arm(Match *m, size_t ast, size_t *typed,
                                 size_t *type, bool is_else)
{
    CheckContext *ctx = m->ctx;
    const TypedNode *node;

    if (!check_expression_coerced(ctx, ast, match_expected(m), typed)) {
        if (!ctx->errors->reported) {
            error_report_at(ctx->errors, m->node->pos,
                            MESSAGE_CHECKER_IF_EXPR_MISMATCH);
        }
        return false;
    }
    node = typed_program_node(ctx->program, *typed);
    *type = node == NULL ? TYPED_NO_NODE : node->type_index;
    /* A bad arm before the else is reported by the caller, not as a
       mismatch. */
    if (is_else && m->had_arm_errors) {
        return true;
    }
    if (!match_type_agrees(m, *type)) {
        error_report_at(ctx->errors, m->node->pos,
                        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
        return false;
    }
    return true;
}

static bool match_arm_body(Match *m, size_t body_ast, size_t narrowing_start,
                           size_t condition)
{
    CheckContext *ctx = m->ctx;
    size_t body;
    size_t body_type = TYPED_NO_NODE;
    bool ok = m->expression_form
                  ? match_expression_arm(m, body_ast, &body, &body_type, false)
                  : check_block(ctx, body_ast, m->return_type, &body);

    if (!ok) {
        return false;
    }
    ctx->narrowings->length = narrowing_start;
    return checker_ok(ctx->errors, list_push(&m->conditions, &condition),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING) &&
           checker_ok(ctx->errors, list_push(&m->bodies, &body),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING) &&
           checker_ok(ctx->errors, list_push(&m->body_types, &body_type),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* One arm. A bad arm is recorded and the rest are still checked, so one pass
   reports every bad arm; running out of memory or room ends the check. */
static ArmResult match_check_arm(Match *m, size_t i)
{
    CheckContext *ctx = m->ctx;
    size_t pattern_ast = match_child(m, i);
    size_t body_ast = match_child(m, i + 1);
    size_t narrowing_start = ctx->narrowings->length;
    Checkpoint checkpoint = checker_checkpoint(ctx);
    size_t condition;
    bool ok;

    if (match_head_type(m) == NULL) {
        match_unsupported(m);
        return ARM_STOP;
    }
    ok = (m->type_match ? match_type_condition(m, pattern_ast, &condition)
                        : match_value_condition(m, pattern_ast, &condition)) &&
         match_arm_body(m, body_ast, narrowing_start, condition);
    if (ok) {
        return ARM_OK;
    }
    ctx->narrowings->length = narrowing_start;
    if (!ctx->errors->reported) {
        error_unknown_if_none(ctx->errors);
    }
    if (checker_cannot_recover(ctx->errors)) {
        return ARM_STOP;
    }
    checker_restore(ctx, checkpoint);
    m->had_arm_errors = true;
    ctx->errors->reported = false;
    return ARM_OK;
}

static bool match_check_arms(Match *m)
{
    size_t i;

    for (i = 0; i < m->node->children_count; i += 2) {
        if (match_check_arm(m, i) == ARM_STOP) {
            return false;
        }
    }
    return true;
}

/* ---- completeness ---- */

/* Some arm tests alternative `alternative`. */
static bool match_covers(const Match *m, size_t alternative)
{
    CheckContext *ctx = m->ctx;
    size_t j;

    for (j = 0; j < m->node->children_count; j += 2) {
        size_t pattern_type;

        if (resolve_type_in_env(ctx, match_child(m, j), ctx->env, "match",
                                &pattern_type) &&
            same_type(ctx->program, alternative, pattern_type)) {
            return true;
        }
    }
    return false;
}

static bool match_covers_union(const Match *m, const TypedType *head_type)
{
    CheckContext *ctx = m->ctx;
    size_t first = head_type->first_child;
    size_t count = head_type->child_count;
    size_t i;

    for (i = 0; i < count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &ctx->program->type_children, first + i);

        if (!match_covers(m, alternative)) {
            return false;
        }
    }
    return true;
}

static bool match_covers_bool(const Match *m)
{
    bool seen_true = false;
    bool seen_false = false;
    size_t i;

    for (i = 0; i < m->node->children_count; i += 2) {
        const AstNode *pattern = ast_get_node(m->ctx->ast, match_child(m, i));

        if (pattern != NULL && pattern->kind == AST_BOOL_LITERAL) {
            seen_true = seen_true || pattern->integer_value != 0;
            seen_false = seen_false || pattern->integer_value == 0;
        }
    }
    return seen_true && seen_false;
}

/* Without an else, the arms must between them cover every value: all the
   alternatives of a union, both booleans, or the one value of Nil. A type
   match on a single type needs one arm. */
static bool match_is_complete(const Match *m, const TypedType *head_type)
{
    const TypedProgram *program = m->ctx->program;

    if (m->type_match) {
        return head_type->kind == NYX_TYPE_UNION
                   ? match_covers_union(m, head_type)
                   : m->body_types.length > 0;
    }
    if (same_type(program, m->head_type_index, m->bool_type)) {
        return match_covers_bool(m);
    }
    return same_type(program, m->head_type_index, m->nil_type);
}

static bool match_check_else(Match *m, size_t *else_type)
{
    CheckContext *ctx = m->ctx;

    if (!m->expression_form) {
        return check_block(ctx, m->node->right_child, m->return_type,
                           &m->else_typed);
    }
    if (!match_expression_arm(m, m->node->right_child, &m->else_typed,
                              else_type, true)) {
        return false;
    }
    ctx->errors->reported = ctx->errors->reported || m->had_arm_errors;
    return !m->had_arm_errors;
}

/* Whether the arms (and else) are acceptable together. */
static bool match_check_completion(Match *m)
{
    CheckContext *ctx = m->ctx;
    const TypedType *head_type;
    size_t else_type = TYPED_NO_NODE;

    if (m->had_arm_errors && !m->has_else) {
        ctx->errors->reported = true;
        return false;
    }
    if (!m->has_else) {
        head_type = match_head_type(m);
        if (head_type == NULL) {
            return match_unsupported(m);
        }
        m->exhaustive = match_is_complete(m, head_type);
    }
    if (m->has_else && !match_check_else(m, &else_type)) {
        return false;
    }
    if (m->had_arm_errors) {
        ctx->errors->reported = true;
        return false;
    }
    if (!m->exhaustive) {
        error_report_at(ctx->errors, m->node->pos,
                        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
        return false;
    }
    return true;
}

/* ---- lowering ---- */

static size_t match_list_item(const List *list, size_t i)
{
    return *(const size_t *)list_get(list, i);
}

/* Wrap `chain` in a block unless it is one, so an if's else is a block. */
static bool match_wrap_in_block(Match *m)
{
    CheckContext *ctx = m->ctx;
    const TypedNode *chain_node = typed_program_node(ctx->program, m->chain);
    TypedNode wrapper = {0};
    size_t wrapper_start = ctx->program->children.length;

    if (m->chain == TYPED_NO_NODE || chain_node == NULL ||
        chain_node->kind == TYPED_BLOCK) {
        return true;
    }
    checker_ok(ctx->errors, typed_program_push_child(ctx->program, m->chain),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    wrapper.kind = TYPED_BLOCK;
    wrapper.name_index = TYPED_NO_NODE;
    wrapper.type_index = TYPED_NO_NODE;
    wrapper.pos = m->node->pos;
    wrapper.children_start = wrapper_start;
    wrapper.children_count = 1;
    return checker_push_node(ctx, &wrapper, &m->chain,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* `if condition then body else chain` for arm `i`. */
static bool match_lower_arm(Match *m, size_t i)
{
    CheckContext *ctx = m->ctx;
    TypedNode lowered = {0};

    lowered.name_index = TYPED_NO_NODE;
    lowered.pos = m->node->pos;
    lowered.left_child = match_list_item(&m->conditions, i);
    if (m->expression_form) {
        lowered.kind = TYPED_IF_EXPRESSION;
        lowered.type_index = match_list_item(&m->body_types, i);
        lowered.right_child = match_list_item(&m->bodies, i);
        lowered.children_start = ctx->program->children.length;
        lowered.children_count = 1;
        checker_ok(ctx->errors,
                   m->chain != TYPED_NO_NODE &&
                       typed_program_push_child(ctx->program, m->chain),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    } else {
        if (!match_wrap_in_block(m)) {
            return false;
        }
        lowered.kind = TYPED_IF;
        lowered.type_index = TYPED_NO_NODE;
        lowered.children_start = ctx->program->children.length;
        lowered.children_count = m->chain == TYPED_NO_NODE ? 1 : 2;
        checker_ok(ctx->errors,
                   typed_program_push_child(ctx->program,
                                            match_list_item(&m->bodies, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        if (m->chain != TYPED_NO_NODE) {
            checker_ok(ctx->errors,
                       typed_program_push_child(ctx->program, m->chain),
                       MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        }
    }
    return checker_push_node(ctx, &lowered, &m->chain,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

/* The arms, last to first, around the else. A statement match that covers
   every case without an else uses its last arm as the final else. */
static bool match_lower(Match *m)
{
    size_t i = m->bodies.length;

    m->chain = m->else_typed;
    if (!m->expression_form && !m->has_else && m->exhaustive && i > 0) {
        m->chain = match_list_item(&m->bodies, --i);
    }
    for (; i > 0; i--) {
        if (!match_lower_arm(m, i - 1)) {
            return false;
        }
    }
    if (m->chain == TYPED_NO_NODE) {
        error_report_at(m->ctx->errors, m->node->pos,
                        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
        return false;
    }
    return true;
}

/* Bind the temporary to the scrutinee around the whole chain. */
static bool match_bind_chain(Match *m)
{
    const TypedNode *result = typed_program_node(m->ctx->program, m->chain);
    TypedNode bind = {0};

    if (m->temporary_index == TYPED_NO_NODE) {
        return true;
    }
    bind.kind = m->expression_form ? TYPED_MATCH_BIND_EXPRESSION
                                   : TYPED_MATCH_BIND_STATEMENT;
    bind.type_index = m->expression_form ? result->type_index : TYPED_NO_NODE;
    bind.pos = m->node->pos;
    bind.name_index = TYPED_NO_NODE;
    bind.left_child = m->original_head;
    bind.right_child = m->chain;
    bind.integer_value_u = m->temporary_index;
    return checker_push_node(m->ctx, &bind, &m->chain,
                             MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

bool check_match(CheckContext *ctx, const AstNode *node, size_t ast_index,
                 bool expression_form, size_t return_type, size_t *out_typed)
{
    Match m;
    bool ok;

    match_open(&m, ctx, node, ast_index, expression_form, return_type);
    ok = match_check_head(&m) && match_check_arms(&m) &&
         match_check_completion(&m) && match_lower(&m) && match_bind_chain(&m);
    if (ok) {
        *mapping_slot(ctx->mapping, ast_index) = m.chain;
        *out_typed = m.chain;
    }
    match_close(&m);
    return ok;
}
