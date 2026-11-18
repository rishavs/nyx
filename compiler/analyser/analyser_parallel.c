#include "analyser.h"
#include "analyser_internal.h"

#include "messages.h"

#include <string.h>

/* An arm runs beside other threads, so everything it can reach must be safe to
   run beside them. A global Store has no owner but the program, and a call
   through a function value could reach anything, so neither is allowed. The
   audit follows every named call from the arm's function, once each. */

typedef struct {
    size_t node;
    size_t function; /* the function declaration the node is in */
} AuditStep;

typedef struct {
    const TypedProgram *program;
    ErrorState *errors;
    List pending; /* AuditStep */
    List seen;    /* function declarations already entered */
} Audit;

static size_t function_named(const TypedProgram *program, size_t name_index)
{
    const char *wanted = typed_program_symbol_text(program, name_index);
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node->kind == TYPED_FUNCTION_DECLARATION &&
            strcmp(typed_program_symbol_text(program, node->name_index),
                   wanted) == 0) {
            return root;
        }
    }
    return TYPED_NO_NODE;
}

static bool out_of_memory(Audit *a)
{
    error_report(a->errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
    return false;
}

static bool push_step(Audit *a, size_t node, size_t function)
{
    AuditStep step = {node, function};

    if (node == TYPED_NO_NODE) {
        return true;
    }
    return list_push(&a->pending, &step) || out_of_memory(a);
}

static bool push_children(Audit *a, const TypedNode *node, size_t function)
{
    size_t i;
    bool ok = push_step(a, node->left_child, function) &&
              push_step(a, node->right_child, function);

    for (i = 0; ok && i < node->children_count; i++) {
        ok = push_step(a, *(const size_t *)list_get(&a->program->children,
                                                    node->children_start + i),
                       function);
    }
    return ok;
}

static bool already_entered(const Audit *a, size_t function)
{
    size_t i;

    for (i = 0; i < a->seen.length; i++) {
        if (*(const size_t *)list_get(&a->seen, i) == function) {
            return true;
        }
    }
    return false;
}

/* Enter the function a call names, once. `label` names the caller in the
   diagnostic when the call has no named target. */
static bool enter_function(Audit *a, SourcePos pos, size_t name_index,
                           const char *label)
{
    size_t callee = function_named(a->program, name_index);

    if (callee == TYPED_NO_NODE) {
        error_report_at(a->errors, pos, MESSAGE_ANALYSER_PARALLEL_INDIRECT, label);
        return false;
    }
    if (already_entered(a, callee)) {
        return true;
    }
    return (list_push(&a->seen, &callee) || out_of_memory(a)) &&
           push_step(a, typed_program_node(a->program, callee)->left_child,
                     callee);
}

static bool audit_step(Audit *a, const AuditStep *step)
{
    const TypedNode *node = typed_program_node(a->program, step->node);
    const char *owner = typed_program_symbol_text(
        a->program,
        typed_program_node(a->program, step->function)->name_index);

    if (analyser_node_uses_store(a->program, node) &&
        node->store_id != TYPED_NO_NODE) {
        error_report_at(a->errors, node->pos,
                        MESSAGE_ANALYSER_PARALLEL_GLOBAL_STORE, owner);
        return false;
    }
    return (node->kind != TYPED_FUNCTION_CALL ||
            enter_function(a, node->pos, node->name_index, owner)) &&
           push_children(a, node, step->function);
}

static bool audit_arm(const TypedProgram *program, ErrorState *errors,
                      const TypedNode *arm)
{
    const TypedNode *call = analyser_parallel_arm_call(program, arm);
    Audit a = {.program = program, .errors = errors};
    AuditStep step;
    bool ok;

    list_init(&a.pending, sizeof(AuditStep));
    list_init(&a.seen, sizeof(size_t));
    ok = enter_function(&a, arm->pos, call->name_index,
                        typed_program_symbol_text(program, call->name_index));
    while (ok && a.pending.length != 0) {
        list_pop(&a.pending, &step);
        ok = audit_step(&a, &step);
    }
    list_free(&a.pending);
    list_free(&a.seen);
    return ok;
}

bool analyser_audit_parallel(const TypedProgram *program, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node->kind == TYPED_PARALLEL_ARM &&
            !audit_arm(program, errors, node)) {
            return false;
        }
    }
    return true;
}
