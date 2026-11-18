#include "analyser.h"
#include "analyser_internal.h"

#include "alloc.h"
#include "messages.h"

#include <stdlib.h>
#include <string.h>

/* Store capability ownership. A node that allocates names a Store: a global
   one by id, or a local one (a parameter or an enclosing `scope` or pool) by
   name. This pass proves every such name resolves, and that global Store ids
   are exactly 0..count-1, because the generator indexes its Store table by id. */

static const TypedNode *root_node(const TypedProgram *program, size_t index)
{
    return typed_program_node(program,
                              *(const size_t *)list_get(&program->roots, index));
}

static const TypedNodeKind store_users[] = {
    TYPED_STORE_ALLOC,     TYPED_STRING_CONCAT, TYPED_STRING_COPY,
    TYPED_STRING_FROM_FIXED, TYPED_BUILDER_CREATE, TYPED_LIST_CREATE,
    TYPED_TABLE_CREATE,    TYPED_CHANNEL_CREATE,
};

bool analyser_node_uses_store(const TypedProgram *program, const TypedNode *node)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(store_users); i++) {
        if (node->kind == store_users[i]) {
            return true;
        }
    }
    return node->kind == TYPED_IDENTIFIER && type != NULL &&
           type->kind == NYX_TYPE_STORE;
}

/* ---- the global Stores ---- */

static bool store_id_taken_before(const TypedProgram *program, size_t end,
                                  size_t id)
{
    size_t i;

    for (i = 0; i < end; i++) {
        const TypedNode *earlier = root_node(program, i);

        if (earlier->kind == TYPED_STORE_DECLARATION && earlier->store_id == id) {
            return true;
        }
    }
    return false;
}

/* A Store root is a Store, has a non-negative capacity, and holds an id no
   earlier root holds. */
static bool store_root_is_valid(const TypedProgram *program, size_t index,
                                const TypedNode *node)
{
    const TypedType *type = typed_program_type(program, node->type_index);

    return type != NULL && type->kind == NYX_TYPE_STORE &&
           node->integer_value >= 0 &&
           !store_id_taken_before(program, index, node->store_id);
}

static bool validate_store_roots(const TypedProgram *program, ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = root_node(program, i);

        if (node->kind == TYPED_STORE_DECLARATION &&
            !store_root_is_valid(program, i, node)) {
            error_report_at(errors, node->pos, MESSAGE_ANALYSER_INVALID_STORE_ID,
                            *(const size_t *)list_get(&program->roots, i));
            return false;
        }
    }
    return true;
}

/* ---- the local Stores ---- */

typedef struct {
    size_t node;
    size_t scope; /* the innermost scope link the node is under */
} ScopeVisit;

typedef struct {
    size_t name;
    size_t parent;
    size_t node;
} ScopeLink;

/* The last (root, scope) pair that reached each node, so a node is visited once
   per pair and the pass costs O(1) per node. */
typedef struct {
    size_t root;
    size_t scope;
} Reached;

typedef struct {
    const TypedProgram *program;
    ErrorState *errors;
    size_t store_count;
    const TypedNode *function; /* the root being walked, when it is a function */
    List pending;              /* ScopeVisit */
    List scopes;               /* ScopeLink */
    Reached *reached;
} StoreWalk;

static bool out_of_memory(StoreWalk *w)
{
    error_report(w->errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
    return false;
}

static bool push_visit(StoreWalk *w, size_t node, size_t scope)
{
    ScopeVisit visit = {node, scope};

    if (node == TYPED_NO_NODE) {
        return true;
    }
    return list_push(&w->pending, &visit) || out_of_memory(w);
}

static bool scope_has_name(const StoreWalk *w, size_t index, size_t name)
{
    while (index != TYPED_NO_NODE) {
        const ScopeLink *scope = list_get(&w->scopes, index);

        if (scope->name == name) {
            return true;
        }
        index = scope->parent;
    }
    return false;
}

static bool scope_chain_has_node(const StoreWalk *w, size_t index, size_t node)
{
    while (index != TYPED_NO_NODE) {
        const ScopeLink *scope = list_get(&w->scopes, index);

        if (scope->node == node) {
            return true;
        }
        index = scope->parent;
    }
    return false;
}

static bool is_store_parameter_named(const StoreWalk *w, size_t name)
{
    const TypedProgram *program = w->program;
    size_t i;

    for (i = 0; w->function != NULL && i < w->function->children_count; i++) {
        const TypedNode *parameter = typed_program_node(
            program, *(const size_t *)list_get(
                         &program->children, w->function->children_start + i));
        const TypedType *type = typed_program_type(program, parameter->type_index);

        if (parameter->kind == TYPED_PARAMETER && parameter->name_index == name &&
            type != NULL && type->kind == NYX_TYPE_STORE) {
            return true;
        }
    }
    return false;
}

/* A Store use names a global by id, or a local by name. */
static bool store_use_resolves(const StoreWalk *w, const ScopeVisit *visit,
                               const TypedNode *node)
{
    if (!analyser_node_uses_store(w->program, node)) {
        return true;
    }
    if (node->store_id != TYPED_NO_NODE) {
        return node->store_id < w->store_count;
    }
    return is_store_parameter_named(w, node->name_index) ||
           scope_has_name(w, visit->scope, node->name_index);
}

/* A scope, and a pool, hide their Store from their own capacity; the Store is
   visible in the body, the arms and the then section. */
static bool opens_scope(const TypedNode *node)
{
    return node->kind == TYPED_SCOPE ||
           (node->kind == TYPED_PARALLEL && node->integer_value == 1);
}

static bool open_scope(StoreWalk *w, const ScopeVisit *visit,
                       const TypedNode *node, size_t *inner)
{
    ScopeLink link = {node->name_index, visit->scope, visit->node};

    if (scope_chain_has_node(w, visit->scope, visit->node)) {
        error_report_at(w->errors, node->pos, MESSAGE_ANALYSER_INVALID_NODE_LINK,
                        visit->node);
        return false;
    }
    *inner = w->scopes.length;
    return list_push(&w->scopes, &link) || out_of_memory(w);
}

static bool push_children(StoreWalk *w, const TypedNode *node, size_t outer,
                          size_t inner)
{
    size_t i;
    bool ok = push_visit(w, node->left_child, outer) &&
              push_visit(w, node->right_child, inner);

    for (i = 0; ok && i < node->children_count; i++) {
        ok = push_visit(w, *(const size_t *)list_get(&w->program->children,
                                                     node->children_start + i),
                        inner);
    }
    return ok;
}

static bool walk_node(StoreWalk *w, const ScopeVisit *visit)
{
    const TypedNode *node = typed_program_node(w->program, visit->node);
    size_t inner = visit->scope;

    if (!store_use_resolves(w, visit, node)) {
        error_report_at(w->errors, node->pos, MESSAGE_ANALYSER_INVALID_STORE_ID,
                        visit->node);
        return false;
    }
    if (opens_scope(node) && !open_scope(w, visit, node, &inner)) {
        return false;
    }
    return push_children(w, node, visit->scope, inner);
}

static bool already_reached(StoreWalk *w, size_t root, const ScopeVisit *visit)
{
    Reached *reached = &w->reached[visit->node];
    bool seen = reached->root == root + 1 && reached->scope == visit->scope;

    reached->root = root + 1;
    reached->scope = visit->scope;
    return seen;
}

static bool walk_root(StoreWalk *w, size_t root)
{
    const TypedNode *node = root_node(w->program, root);
    bool is_function = node->kind == TYPED_FUNCTION_DECLARATION;
    ScopeVisit visit;

    w->function = is_function ? node : NULL;
    w->scopes.length = 0;
    if (!push_visit(w, is_function
                           ? node->left_child
                           : *(const size_t *)list_get(&w->program->roots, root),
                    TYPED_NO_NODE)) {
        return false;
    }
    while (w->pending.length != 0) {
        list_pop(&w->pending, &visit);
        if (!already_reached(w, root, &visit) && !walk_node(w, &visit)) {
            return false;
        }
    }
    return true;
}

bool analyser_validate_stores(const TypedProgram *program, size_t store_count,
                              ErrorState *errors)
{
    StoreWalk w = {.program = program, .errors = errors,
                   .store_count = store_count};
    size_t i;
    bool ok;

    if (!validate_store_roots(program, errors)) {
        return false;
    }
    list_init(&w.pending, sizeof(ScopeVisit));
    list_init(&w.scopes, sizeof(ScopeLink));
    w.reached = nyx_malloc((program->nodes.length + 1) * sizeof(Reached));
    ok = w.reached != NULL || out_of_memory(&w);
    if (ok) {
        memset(w.reached, 0, (program->nodes.length + 1) * sizeof(Reached));
    }
    for (i = 0; ok && i < program->roots.length; i++) {
        ok = walk_root(&w, i);
    }
    list_free(&w.pending);
    list_free(&w.scopes);
    free(w.reached);
    return ok;
}
