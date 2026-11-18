#include "analyser.h"

#include "alloc.h"
#include "messages.h"

#include <stdlib.h>
#include <string.h>

/* The facts are read off two tables, one keyed by type kind and one by node
   kind, plus the few cases that depend on more than the kind. Adding a runtime
   feature is a new bit in analysis.h and a row here. */

#define FEATURE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

static const uint32_t type_features[NYX_TYPE_KIND_COUNT] = {
    [NYX_TYPE_STRING] = ANALYSIS_STRING,
    [NYX_TYPE_ERROR] = ANALYSIS_STRING,
    [NYX_TYPE_TEXT_BUILDER] = ANALYSIS_STRING_BUILDER,
    [NYX_TYPE_DYNAMIC_STRING] = ANALYSIS_DYNAMIC_STRING,
    [NYX_TYPE_LIST] = ANALYSIS_LIST | ANALYSIS_DYNAMIC_LIST,
    [NYX_TYPE_FIXED_LIST] = ANALYSIS_LIST,
    [NYX_TYPE_SLICE] = ANALYSIS_SLICE,
    [NYX_TYPE_STORE] = ANALYSIS_STORE_OR_HANDLE,
    [NYX_TYPE_HANDLE] = ANALYSIS_STORE_OR_HANDLE,
    [NYX_TYPE_RANGE] = ANALYSIS_RANGE,
    [NYX_TYPE_RECORD] = ANALYSIS_RECORD,
    [NYX_TYPE_TABLE] = ANALYSIS_TABLE,
    [NYX_TYPE_CHANNEL] = ANALYSIS_CHANNEL_VALUES
};

static const uint32_t node_features[TYPED_NODE_KIND_COUNT] = {
    [TYPED_STRING_LITERAL] = ANALYSIS_STRING,
    [TYPED_PRINT] = ANALYSIS_PRINT,
    [TYPED_CORES] = ANALYSIS_CORES,
    [TYPED_CHANNEL_DECLARATION] = ANALYSIS_CHANNELS,
    [TYPED_CHANNEL_OP] = ANALYSIS_CHANNELS,
    [TYPED_CHANNEL_CREATE] = ANALYSIS_CHANNELS,
    [TYPED_WHEN] = ANALYSIS_CHANNELS,
    [TYPED_PARALLEL] = ANALYSIS_PARALLEL,
    [TYPED_ATOMIC_DECLARATION] = ANALYSIS_ATOMICS,
    [TYPED_ATOMIC_LOAD] = ANALYSIS_ATOMICS,
    [TYPED_ATOMIC_STORE] = ANALYSIS_ATOMICS,
    [TYPED_ATOMIC_ADD] = ANALYSIS_ATOMICS,
    [TYPED_CLOCK_NOW] = ANALYSIS_NOW,
    [TYPED_SLEEP] = ANALYSIS_SLEEP,
    [TYPED_SCOPE] = ANALYSIS_SCOPE
};

/* The operators whose C lowering needs the checked-arithmetic helpers. */
static const uint32_t operator_features[NYX_OPERATOR_COUNT] = {
    [NYX_OPERATOR_DIV] = ANALYSIS_CHECKED_ARITHMETIC,
    [NYX_OPERATOR_MOD] = ANALYSIS_CHECKED_ARITHMETIC,
    [NYX_OPERATOR_SHL] = ANALYSIS_CHECKED_ARITHMETIC,
    [NYX_OPERATOR_SHR] = ANALYSIS_CHECKED_ARITHMETIC
};

/* A conversion to the same type, or a folded constant, emits no helper call. */
static uint32_t conversion_features(const TypedProgram *program,
                                    const TypedNode *node)
{
    const TypedType *target = typed_program_type(program, node->type_index);
    const TypedType *source = typed_program_type(program, node->auxiliary_type);

    return target->kind != source->kind ? ANALYSIS_CONVERSION : 0;
}

/* Runtime parts that need another: when any of `when` is set, `adds` is too.
   Applied in this order, so a row may rely on the rows above it. */
static const struct {
    uint32_t when;
    uint32_t adds;
} implications[] = {
    {ANALYSIS_STRING_BUILDER, ANALYSIS_DYNAMIC_STRING},
    {ANALYSIS_DYNAMIC_STRING | ANALYSIS_DYNAMIC_LIST | ANALYSIS_SLICE |
         ANALYSIS_TABLE,
     ANALYSIS_STORE_OR_HANDLE}
};

/* What a comparison between two numeric domains needs. Each row is a pair of
   predicates on the operand types; either order matches. */
typedef struct {
    bool (*first)(NyxType);
    bool (*second)(NyxType);
    uint32_t feature;
} MixedEquality;

static const MixedEquality mixed_equalities[] = {
    {nyx_type_is_signed_integer, nyx_type_is_unsigned_integer,
     ANALYSIS_EQUALITY_SIGNED_UNSIGNED},
    {nyx_type_is_signed_integer, nyx_type_is_float,
     ANALYSIS_EQUALITY_SIGNED_FLOAT},
    {nyx_type_is_unsigned_integer, nyx_type_is_float,
     ANALYSIS_EQUALITY_UNSIGNED_FLOAT}
};

static uint32_t equality_features(const TypedProgram *program,
                                  const TypedNode *node)
{
    NyxType a = typed_program_node_kind(program, node->left_child);
    NyxType b = typed_program_node_kind(program, node->right_child);
    uint32_t features = 0;
    size_t i;

    for (i = 0; i < FEATURE_COUNT(mixed_equalities); i++) {
        const MixedEquality *row = &mixed_equalities[i];

        if ((row->first(a) && row->second(b)) ||
            (row->second(a) && row->first(b))) {
            features |= row->feature;
        }
    }
    return features;
}

static uint32_t binary_features(const TypedProgram *program,
                                const TypedNode *node)
{
    bool equality = node->op == NYX_OPERATOR_EQ || node->op == NYX_OPERATOR_NE;

    return operator_features[node->op] |
           (equality ? equality_features(program, node) : 0);
}

/* A pool is a Store, so it is a scope to the escape analysis. */
static uint32_t parallel_features(const TypedProgram *program,
                                  const TypedNode *node)
{
    (void)program;
    return node->integer_value == 1 ? ANALYSIS_POOL | ANALYSIS_SCOPE : 0;
}

/* The features that depend on more than the node's kind. */
static const struct {
    TypedNodeKind kind;
    uint32_t (*features)(const TypedProgram *, const TypedNode *);
} extra_features[] = {
    {TYPED_BINARY, binary_features},
    {TYPED_NUMERIC_CONVERSION, conversion_features},
    {TYPED_PARALLEL, parallel_features},
};

static uint32_t node_extra_features(const TypedProgram *program,
                                    const TypedNode *node)
{
    uint32_t features = 0;
    size_t i;

    for (i = 0; i < FEATURE_COUNT(extra_features); i++) {
        if (extra_features[i].kind == node->kind) {
            features |= extra_features[i].features(program, node);
        }
    }
    return features;
}

#define NO_OWNER ((size_t)-1)
/* Trees in a typed program are as deep as the source allows; this only stops a
   hand-built cycle or a chain no source could write. */
#define MAX_CLAIM_DEPTH 8192

typedef struct {
    const TypedProgram *program;
    size_t *owner;
    size_t root;
} Claim;

static void claim_child(Claim *claim, size_t index, size_t depth);

/* Gives the root every node in the tree below `index`. */
static void claim_node(Claim *claim, size_t index, size_t depth)
{
    const TypedNode *node = typed_program_node(claim->program, index);
    size_t i;

    claim->owner[index] = claim->root;
    claim_child(claim, node->left_child, depth + 1);
    claim_child(claim, node->right_child, depth + 1);
    for (i = 0; i < node->children_count; i++) {
        claim_child(claim,
                    *(const size_t *)list_get(&claim->program->children,
                                              node->children_start + i),
                    depth + 1);
    }
}

/* A nested function declaration is a root of its own, so a walk stops at one. */
static void claim_child(Claim *claim, size_t index, size_t depth)
{
    if (index == TYPED_NO_NODE || claim->owner[index] != NO_OWNER ||
        depth > MAX_CLAIM_DEPTH ||
        typed_program_node(claim->program, index)->kind ==
            TYPED_FUNCTION_DECLARATION) {
        return;
    }
    claim_node(claim, index, depth);
}

/* Fills `start` (buckets + 1 entries, zeroed) and `out` so the items with
   each key are contiguous and keep their order: a stable counting sort. `items`
   is the item to place for each key, or NULL for the position itself. */
static void sort_by_key(const size_t *keys, const size_t *items, size_t count,
                        size_t buckets, size_t *start, size_t *out)
{
    size_t i;

    for (i = 0; i < count; i++) {
        start[keys[i] + 1]++;
    }
    for (i = 0; i < buckets; i++) {
        start[i + 1] += start[i];
    }
    for (i = 0; i < count; i++) {
        out[start[keys[i]]++] = items == NULL ? i : items[i];
    }
    for (i = buckets; i > 0; i--) {
        start[i] = start[i - 1];
    }
    start[0] = 0;
}

static bool out_of_memory(ErrorState *errors)
{
    error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
    return false;
}

/* The key of each type: its kind. */
static void index_types(const TypedProgram *program, Analysis *analysis,
                        size_t *keys)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        keys[i] = typed_program_type(program, i)->kind;
    }
    sort_by_key(keys, NULL, program->types.length, NYX_TYPE_KIND_COUNT,
                analysis->type_kind_start, analysis->type_order);
}

/* Which root owns each node, found by claiming the tree below each root. */
static void assign_owners(const TypedProgram *program, size_t *owner)
{
    Claim claim = {program, owner, 0};
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        owner[i] = NO_OWNER;
    }
    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);

        claim.root = i;
        if (owner[root] == NO_OWNER) {
            claim_node(&claim, root, 0);
        }
    }
}

/* Groups the node indices by kind, and within a kind by owning root, with two
   stable counting sorts, so each group stays in node order. The node index, the
   owners, and the type index are one allocation. */
static void index_nodes_sorted(const TypedProgram *program, Analysis *analysis,
                               size_t *keys, size_t *by_owner,
                               size_t *owner_start)
{
    size_t count = program->nodes.length;
    size_t roots = program->roots.length;
    size_t i;

    /* Nodes ordered by owner (those with none last), then stably by kind. */
    for (i = 0; i < count; i++) {
        size_t owner = analysis->node_owner[i];

        keys[i] = owner == NO_OWNER ? roots : owner;
    }
    sort_by_key(keys, NULL, count, roots + 1, owner_start, by_owner);
    for (i = 0; i < count; i++) {
        keys[i] = typed_program_node(program, by_owner[i])->kind;
    }
    sort_by_key(keys, by_owner, count, TYPED_NODE_KIND_COUNT,
                analysis->kind_start, analysis->node_order);
}

static bool index_nodes(const TypedProgram *program, Analysis *analysis,
                        ErrorState *errors)
{
    size_t count = program->nodes.length;
    size_t types = program->types.length;
    size_t roots = program->roots.length;
    size_t keys = count > types ? count : types;
    size_t *scratch;

    if (count == 0 && types == 0) {
        return true;
    }
    analysis->node_order = nyx_malloc((2 * count + types) * sizeof(size_t));
    scratch = nyx_malloc((keys + count + roots + 2) * sizeof(size_t));
    if (analysis->node_order == NULL || scratch == NULL) {
        free(scratch);
        free(analysis->node_order);
        analysis->node_order = NULL;
        return out_of_memory(errors);
    }
    memset(scratch + keys + count, 0, (roots + 2) * sizeof(size_t));
    analysis->node_owner = analysis->node_order + count;
    analysis->type_order = analysis->node_owner + count;
    index_types(program, analysis, scratch);
    assign_owners(program, analysis->node_owner);
    index_nodes_sorted(program, analysis, scratch, scratch + keys,
                       scratch + keys + count);
    free(scratch);
    return true;
}

const size_t *analysis_types(const Analysis *analysis, NyxType kind,
                             size_t *count)
{
    if ((size_t)kind >= NYX_TYPE_KIND_COUNT || analysis->type_order == NULL) {
        *count = 0;
        return NULL;
    }
    *count = analysis->type_kind_start[kind + 1] -
             analysis->type_kind_start[kind];
    return analysis->type_order + analysis->type_kind_start[kind];
}

size_t analysis_nil_type(const Analysis *analysis)
{
    size_t count;
    const size_t *nils = analysis_types(analysis, NYX_TYPE_NIL, &count);

    return count == 0 ? TYPED_NO_NODE : nils[0];
}

const size_t *analysis_nodes(const Analysis *analysis, TypedNodeKind kind,
                             size_t *count)
{
    if ((size_t)kind >= TYPED_NODE_KIND_COUNT || analysis->node_order == NULL) {
        *count = 0;
        return NULL;
    }
    *count = analysis->kind_start[kind + 1] - analysis->kind_start[kind];
    return analysis->node_order + analysis->kind_start[kind];
}

const size_t *analysis_nodes_in(const Analysis *analysis, TypedNodeKind kind,
                                size_t root, size_t *count)
{
    size_t total;
    const size_t *nodes = analysis_nodes(analysis, kind, &total);
    size_t low = 0;
    size_t high = total;

    /* The first node of the kind whose owner is `root` or later. */
    while (low < high) {
        size_t middle = low + (high - low) / 2;

        if (analysis->node_owner[nodes[middle]] < root) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    high = low;
    while (high < total && analysis->node_owner[nodes[high]] == root) {
        high++;
    }
    *count = high - low;
    return nodes == NULL ? NULL : nodes + low;
}

void analysis_free(Analysis *analysis)
{
    free(analysis->node_order);
    analysis->node_order = NULL;
    analysis->node_owner = NULL;
    analysis->type_order = NULL;
    list_free(&analysis->type_emit_order);
    list_free(&analysis->print_types);
    list_free(&analysis->equal_types);
    list_free(&analysis->equal_pairs);
}

/* Direct recursion: a function whose body calls the function by its own name. */
static bool function_calls_itself(const TypedProgram *program,
                                  const Analysis *analysis, size_t position,
                                  const TypedNode *function)
{
    size_t calls, i;
    const size_t *call_nodes =
        analysis_nodes_in(analysis, TYPED_FUNCTION_CALL, position, &calls);

    for (i = 0; i < calls; i++) {
        if (typed_program_node(program, call_nodes[i])->name_index ==
            function->name_index) {
            return true;
        }
    }
    return false;
}

static bool any_function_calls_itself(const TypedProgram *program,
                                      const Analysis *analysis)
{
    size_t position;

    for (position = 0; position < program->roots.length; position++) {
        const TypedNode *function = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, position));

        if (function->kind == TYPED_FUNCTION_DECLARATION &&
            function_calls_itself(program, analysis, position, function)) {
            return true;
        }
    }
    return false;
}

/* The features each type and node asks for, and the Stores among the roots. */
static uint32_t program_features(const TypedProgram *program, size_t *stores)
{
    uint32_t features = 0;
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        features |= type_features[typed_program_type(program, i)->kind];
    }
    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);

        features |= node_features[node->kind] |
                    node_extra_features(program, node);
    }
    for (i = 0; i < program->roots.length; i++) {
        *stores += typed_program_node(
                       program, *(const size_t *)list_get(&program->roots, i))
                       ->kind == TYPED_STORE_DECLARATION;
    }
    return features;
}

static uint32_t imply_features(uint32_t features)
{
    size_t i;

    for (i = 0; i < FEATURE_COUNT(implications); i++) {
        if ((features & implications[i].when) != 0) {
            features |= implications[i].adds;
        }
    }
    return features;
}

bool analysis_collect(const TypedProgram *program, Analysis *analysis,
                      ErrorState *errors)
{
    memset(analysis, 0, sizeof(*analysis));
    list_init(&analysis->type_emit_order, sizeof(size_t));
    list_init(&analysis->print_types, sizeof(size_t));
    list_init(&analysis->equal_types, sizeof(size_t));
    list_init(&analysis->equal_pairs, sizeof(size_t));
    analysis->features = program_features(program, &analysis->store_count);
    if (analysis->store_count > 0) {
        analysis->features |= ANALYSIS_STORE_OR_HANDLE;
    }
    analysis->features = imply_features(analysis->features);
    if (!index_nodes(program, analysis, errors)) {
        analysis_free(analysis);
        return false;
    }
    if (any_function_calls_itself(program, analysis)) {
        analysis->features |= ANALYSIS_RECURSION;
    }
    if (!analysis_collect_types(program, analysis, errors)) {
        analysis_free(analysis);
        return false;
    }
    return true;
}
