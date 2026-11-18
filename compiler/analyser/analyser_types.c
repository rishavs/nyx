#include "analyser.h"

#include "alloc.h"
#include "macros.h"
#include "messages.h"

#include <stdlib.h>
#include <string.h>

/* Facts about types that the generator emits from: the order to define them in,
   and which ones print and equality need helpers for. They are lists of type
   indices, worked out here once so the generator walks a list instead of the
   program. */

/* ---- the order to define types in ---- */

typedef struct {
    size_t index;
    size_t next;
} OrderFrame;

/* What a type depends on, in order: how many dependencies it has and the
   `at`th. Validation has proved every one is a type of the program. */
typedef size_t (*DependencyCount)(const TypedProgram *, const TypedType *);
typedef size_t (*DependencyAt)(const TypedProgram *, const TypedType *, size_t);

static size_t fields_count(const TypedProgram *program, const TypedType *type)
{
    return typed_program_node(program, type->declaration_node)->children_count;
}

static size_t fields_at(const TypedProgram *program, const TypedType *type,
                        size_t at)
{
    const TypedNode *declaration =
        typed_program_node(program, type->declaration_node);

    return typed_program_node(
               program, *(const size_t *)list_get(&program->children,
                                                  declaration->children_start +
                                                      at))
        ->type_index;
}

static size_t parts_count(const TypedProgram *program, const TypedType *type)
{
    (void)program;
    return type->child_count;
}

static size_t parts_at(const TypedProgram *program, const TypedType *type,
                       size_t at)
{
    return *(const size_t *)list_get(&program->type_children,
                                     type->first_child + at);
}

/* A function type depends on its return type, then its parameters. */
static size_t signature_count(const TypedProgram *program, const TypedType *type)
{
    return 1 + typed_program_signature(program, type->signature_index)
                   ->parameter_count;
}

static size_t signature_at(const TypedProgram *program, const TypedType *type,
                           size_t at)
{
    const TypedFunctionSignature *signature =
        typed_program_signature(program, type->signature_index);

    return at == 0 ? signature->return_type
                   : *(const size_t *)list_get(&program->signature_parameters,
                                               signature->first_parameter +
                                                   at - 1);
}

static const struct {
    NyxType kind;
    DependencyCount count;
    DependencyAt at;
} dependency_rules[] = {
    {NYX_TYPE_STRUCT, fields_count, fields_at},
    {NYX_TYPE_UNION, parts_count, parts_at},
    {NYX_TYPE_FIXED_LIST, parts_count, parts_at},
    {NYX_TYPE_RECORD, parts_count, parts_at},
    {NYX_TYPE_ERROR, parts_count, parts_at},
    {NYX_TYPE_FUNCTION, signature_count, signature_at},
};

/* The `at`th type that type `index` holds by value or points its definition
   at, so that it can be defined first. False when there are no more. */
static bool order_dependency(const TypedProgram *program, size_t index,
                             size_t at, size_t *dependency)
{
    const TypedType *type = typed_program_type(program, index);
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(dependency_rules); i++) {
        if (dependency_rules[i].kind == type->kind &&
            at < dependency_rules[i].count(program, type)) {
            *dependency = dependency_rules[i].at(program, type, at);
            return true;
        }
    }
    return false;
}

/* A depth-first walk from each type, with an explicit stack, that lists a type
   once everything it depends on is listed. A type met again while it is still
   being walked contains itself. */
typedef struct {
    const TypedProgram *program;
    Analysis *analysis;
    ErrorState *errors;
    unsigned char *state; /* 0 unseen, 1 being walked, 2 listed */
    OrderFrame *stack;
    size_t depth;
} OrderWalk;

static void order_enter(OrderWalk *w, size_t index)
{
    w->state[index] = 1;
    w->stack[w->depth++] = (OrderFrame){index, 0};
}

static bool order_finish(OrderWalk *w)
{
    size_t completed = w->stack[--w->depth].index;

    w->state[completed] = 2;
    if (!list_push(&w->analysis->type_emit_order, &completed)) {
        error_report(w->errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool order_step(OrderWalk *w)
{
    OrderFrame *frame = &w->stack[w->depth - 1];
    size_t dependency;

    if (!order_dependency(w->program, frame->index, frame->next++,
                          &dependency)) {
        return order_finish(w);
    }
    if (w->state[dependency] == 1) {
        error_report(w->errors, MESSAGE_ANALYSER_TYPE_ORDER, dependency);
        return false;
    }
    if (w->state[dependency] == 0) {
        order_enter(w, dependency);
    }
    return true;
}

static bool order_types(const TypedProgram *program, Analysis *analysis,
                        ErrorState *errors)
{
    size_t count = program->types.length;
    OrderWalk w = {program, analysis, errors, NULL, NULL, 0};
    size_t i;
    bool ok = true;

    if (count == 0) {
        return true;
    }
    w.state = nyx_malloc(count);
    w.stack = nyx_malloc(count * sizeof(*w.stack));
    if (w.state == NULL || w.stack == NULL) {
        error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        ok = false;
    } else {
        memset(w.state, 0, count);
    }
    for (i = 0; ok && i < count; i++) {
        if (w.state[i] == 0) {
            order_enter(&w, i);
        }
        while (ok && w.depth > 0) {
            ok = order_step(&w);
        }
    }
    free(w.stack);
    free(w.state);
    return ok;
}

/* ---- the types print and equality need helpers for ---- */

/* What a type contributes to the set of types that need a helper. */
typedef enum {
    COLLECT_LEAF,       /* handled directly; nothing more to collect */
    COLLECT_PARTS,      /* its parts need helpers too */
    COLLECT_UNSUPPORTED /* cannot be printed or compared */
} Collect;

static const unsigned char collect_rules[] = {
    [NYX_TYPE_UNION] = COLLECT_PARTS,
    [NYX_TYPE_STRUCT] = COLLECT_PARTS,
    [NYX_TYPE_ERROR] = COLLECT_PARTS,
    [NYX_TYPE_RECORD] = COLLECT_PARTS,
    [NYX_TYPE_TABLE] = COLLECT_PARTS,
    [NYX_TYPE_LIST] = COLLECT_PARTS,
    [NYX_TYPE_FIXED_LIST] = COLLECT_PARTS,
    [NYX_TYPE_SLICE] = COLLECT_PARTS,
    [NYX_TYPE_STORE] = COLLECT_UNSUPPORTED,
    [NYX_TYPE_UNKNOWN] = COLLECT_UNSUPPORTED,
    [NYX_TYPE_STRING_LITERAL] = COLLECT_UNSUPPORTED,
};

static Collect collect_rule(NyxType kind)
{
    return (size_t)kind < NYX_COUNT_OF(collect_rules) ? collect_rules[kind]
                                                      : COLLECT_LEAF;
}

static bool in_list(const List *types, size_t type_index)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        if (*(const size_t *)list_get(types, i) == type_index) {
            return true;
        }
    }
    return false;
}

static bool collect_type(const TypedProgram *program, size_t type_index,
                         List *types, unsigned depth, ErrorState *errors);

static bool unsupported(ErrorState *errors, size_t type_index)
{
    error_report(errors, MESSAGE_ANALYSER_UNSUPPORTED_TYPE, type_index);
    return false;
}

/* Every part of the type needs a helper too. */
static bool collect_parts(const TypedProgram *program, const TypedType *type,
                          List *types, unsigned depth, ErrorState *errors)
{
    size_t count = typed_type_part_count(program, type);
    size_t i;

    for (i = 0; i < count; i++) {
        size_t part;

        typed_type_part(program, type, i, &part);
        if (!collect_type(program, part, types, depth, errors)) {
            return false;
        }
    }
    return true;
}

/* Adds the type and, below it, every type its helper handles. A type the
   program does not have reads as unknown, which no helper can handle. */
static bool collect_type(const TypedProgram *program, size_t type_index,
                         List *types, unsigned depth, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    Collect rule = collect_rule(type == NULL ? NYX_TYPE_UNKNOWN : type->kind);

    if (depth == 0 || rule == COLLECT_UNSUPPORTED) {
        return unsupported(errors, type_index);
    }
    if (in_list(types, type_index)) {
        return true;
    }
    if (!list_push(types, &type_index)) {
        error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        return false;
    }
    return rule != COLLECT_PARTS ||
           collect_parts(program, type, types, depth - 1, errors);
}

/* The deepest a type is followed; print and equality both stop here. */
#define COLLECT_DEPTH_PRINT 32
#define COLLECT_DEPTH_EQUALITY 64

static bool collect_print_arguments(const TypedProgram *program,
                                    Analysis *analysis, const TypedNode *node,
                                    ErrorState *errors)
{
    size_t j;

    for (j = 0; j < node->children_count; j++) {
        const TypedNode *argument = typed_program_node(
            program, *(const size_t *)list_get(&program->children,
                                               node->children_start + j));

        if (!collect_type(program, argument->type_index,
                          &analysis->print_types, COLLECT_DEPTH_PRINT, errors)) {
            return false;
        }
    }
    return true;
}

/* A float anywhere among the printed types, not only as an argument. */
static bool collect_print_types(const TypedProgram *program,
                                Analysis *analysis, ErrorState *errors)
{
    size_t count, i;
    const size_t *prints = analysis_nodes(analysis, TYPED_PRINT, &count);

    for (i = 0; i < count; i++) {
        if (!collect_print_arguments(program, analysis,
                                     typed_program_node(program, prints[i]),
                                     errors)) {
            return false;
        }
    }
    for (i = 0; i < analysis->print_types.length; i++) {
        const TypedType *type = typed_program_type(
            program, *(const size_t *)list_get(&analysis->print_types, i));

        if (nyx_type_is_float(type->kind)) {
            analysis->features |= ANALYSIS_PRINT_FLOAT;
        }
    }
    return true;
}

static bool equality_operator(NyxOperator op)
{
    return op == NYX_OPERATOR_EQ || op == NYX_OPERATOR_NE ||
           op == NYX_OPERATOR_STRICT_EQ;
}

static bool string_kind(NyxType kind)
{
    return kind == NYX_TYPE_STRING || kind == NYX_TYPE_DYNAMIC_STRING;
}

static bool composite_kind(NyxType kind)
{
    return kind == NYX_TYPE_STRUCT || kind == NYX_TYPE_FIXED_LIST ||
           kind == NYX_TYPE_LIST || kind == NYX_TYPE_RECORD ||
           kind == NYX_TYPE_TABLE || kind == NYX_TYPE_HANDLE;
}

static bool pair_listed(const List *pairs, size_t left, size_t right)
{
    size_t i;

    for (i = 0; i + 1 < pairs->length; i += 2) {
        if (*(const size_t *)list_get(pairs, i) == left &&
            *(const size_t *)list_get(pairs, i + 1) == right) {
            return true;
        }
    }
    return false;
}

/* The two operands of a comparison. */
typedef struct {
    size_t left;
    size_t right;      /* the operands' type indices */
    NyxType left_kind;
    NyxType right_kind;
    bool equivalent;   /* the operand types would be the same C type */
    NyxOperator op;
} Comparison;

/* Two different string types compared with == need a pair helper. */
static bool needs_pair_helper(const Analysis *analysis, const Comparison *c)
{
    return string_kind(c->left_kind) && string_kind(c->right_kind) &&
           !c->equivalent && c->op != NYX_OPERATOR_STRICT_EQ &&
           !pair_listed(&analysis->equal_pairs, c->left, c->right);
}

static bool collect_pair(Analysis *analysis, const Comparison *c,
                         ErrorState *errors)
{
    if (!needs_pair_helper(analysis, c)) {
        return true;
    }
    if (!list_push(&analysis->equal_pairs, &c->left) ||
        !list_push(&analysis->equal_pairs, &c->right)) {
        error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

/* One comparison: a handle needs validating; a composite type compared with an
   equivalent one needs a helper; two different string types need a pair
   helper. */
static bool collect_comparison(const TypedProgram *program, Analysis *analysis,
                               const TypedNode *node, ErrorState *errors)
{
    size_t left = typed_program_node(program, node->left_child)->type_index;
    size_t right = typed_program_node(program, node->right_child)->type_index;
    Comparison c = {left, right, typed_program_type(program, left)->kind,
                    typed_program_type(program, right)->kind,
                    typed_types_equivalent(program, left, right,
                                           COLLECT_DEPTH_EQUALITY),
                    node->op};

    if (c.left_kind == NYX_TYPE_HANDLE || c.right_kind == NYX_TYPE_HANDLE) {
        analysis->features |= ANALYSIS_EQUALITY_HANDLE;
    }
    if (composite_kind(c.left_kind) && c.equivalent &&
        !collect_type(program, left, &analysis->equal_types,
                      COLLECT_DEPTH_EQUALITY, errors)) {
        return false;
    }
    return collect_pair(analysis, &c, errors);
}

static bool collect_equality_types(const TypedProgram *program,
                                   Analysis *analysis, ErrorState *errors)
{
    size_t count, i;
    const size_t *binaries = analysis_nodes(analysis, TYPED_BINARY, &count);

    for (i = 0; i < count; i++) {
        const TypedNode *node = typed_program_node(program, binaries[i]);

        if (equality_operator(node->op) &&
            !collect_comparison(program, analysis, node, errors)) {
            return false;
        }
    }
    for (i = 0; i < analysis->equal_types.length; i++) {
        const TypedType *type = typed_program_type(
            program, *(const size_t *)list_get(&analysis->equal_types, i));

        if (type->kind == NYX_TYPE_HANDLE) {
            analysis->features |= ANALYSIS_EQUALITY_HANDLE;
        }
    }
    return true;
}

bool analysis_collect_types(const TypedProgram *program, Analysis *analysis,
                            ErrorState *errors)
{
    return order_types(program, analysis, errors) &&
           collect_print_types(program, analysis, errors) &&
           collect_equality_types(program, analysis, errors);
}
