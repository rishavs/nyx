#ifndef NYX_ANALYSIS_H
#define NYX_ANALYSIS_H

#include "list.h"
#include "typed_program.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the analyser learned about a whole typed program. The generator reads
   it to decide which runtime pieces to emit, and where to find the nodes it
   emits helpers for, instead of scanning the program again for each one. It
   describes one program: build it from the program you hand to the generator.
   It owns one allocation (the indexes below), so a built Analysis is released
   with analysis_free. */
typedef enum {
    ANALYSIS_STRING_BUILDER = 1u << 0, /* implies DYNAMIC_STRING */
    ANALYSIS_LIST = 1u << 1,         /* any List or fixed-length Array */
    ANALYSIS_DYNAMIC_LIST = 1u << 2, /* a growable List */
    ANALYSIS_SLICE = 1u << 3,
    ANALYSIS_DYNAMIC_STRING = 1u << 4, /* a Text or a builder; implies STORE */
    ANALYSIS_STRING = 1u << 5,       /* a String, Error, or string literal */
    /* The store runtime is needed: a Store or Handle, a Store declaration, or
       anything allocated in one (Text, a growable List, a Slice, a Table). */
    ANALYSIS_STORE_OR_HANDLE = 1u << 6,
    ANALYSIS_PRINT = 1u << 7,
    ANALYSIS_CORES = 1u << 8,
    ANALYSIS_CHANNELS = 1u << 9,
    ANALYSIS_PARALLEL = 1u << 10,
    ANALYSIS_POOL = 1u << 11,        /* a parallel block with a pool */
    ANALYSIS_ATOMICS = 1u << 12,
    ANALYSIS_NOW = 1u << 13,         /* now() */
    ANALYSIS_RECURSION = 1u << 14,   /* a function that calls itself */
    ANALYSIS_CONVERSION = 1u << 15,  /* a numeric conversion that needs code */
    ANALYSIS_SCOPE = 1u << 16,       /* a scope, or a parallel pool */
    ANALYSIS_CHECKED_ARITHMETIC = 1u << 17, /* /, %, <<, >> */
    ANALYSIS_RANGE = 1u << 18,
    ANALYSIS_RECORD = 1u << 19,
    ANALYSIS_TABLE = 1u << 20,
    /* ==/!= between operands of different numeric domains: each needs its own
       exact-comparison helper. */
    ANALYSIS_EQUALITY_SIGNED_UNSIGNED = 1u << 21,
    ANALYSIS_EQUALITY_SIGNED_FLOAT = 1u << 22,
    ANALYSIS_EQUALITY_UNSIGNED_FLOAT = 1u << 23,
    ANALYSIS_CHANNEL_VALUES = 1u << 24, /* a Channel type is in use */
    ANALYSIS_SLEEP = 1u << 25, /* sleep(); its result is Nil */
    ANALYSIS_PRINT_FLOAT = 1u << 26, /* a Float32 or Float64 is printed */
    ANALYSIS_EQUALITY_HANDLE = 1u << 27 /* a Handle is compared */
} AnalysisFeature;

typedef struct {
    uint32_t features;
    /* Number of Store declarations among the roots. */
    size_t store_count;
    /* Every node index, grouped by node kind: the nodes of kind K are
       node_order[kind_start[K] .. kind_start[K + 1]). Within a kind they are
       ordered by the root they belong to (its position in the program's roots)
       and then by index. node_owner[i] is the position of the root whose tree
       holds node i, or (size_t)-1 for a node no root reaches. Both arrays are
       one allocation; read them through analysis_nodes and analysis_nodes_in. */
    size_t *node_order;
    size_t *node_owner;
    size_t kind_start[TYPED_NODE_KIND_COUNT + 1];
    /* The same for the type table: type indices grouped by kind, each group in
       ascending order, in the same allocation. Read it through analysis_types. */
    size_t *type_order;
    size_t type_kind_start[NYX_TYPE_KIND_COUNT + 1];
    /* Lists of type indices (size_t) the generator emits from, each built once
       so the generator never works them out from the program itself. */
    List type_emit_order;  /* every type, each after the types it holds by value */
    List print_types;      /* the types print needs a helper for, and their parts */
    List equal_types;      /* the types == and != need a helper for, and their parts */
    List equal_pairs;      /* pairs of string types compared, two entries each */
} Analysis;

/* The indices of the nodes of `kind`, in ascending order, and how many. Not
   NULL-safe on the Analysis; a kind with no nodes gives a count of zero. */
const size_t *analysis_nodes(const Analysis *analysis, TypedNodeKind kind,
                             size_t *count);

/* The indices of the types of `kind`, in ascending order, and how many. */
const size_t *analysis_types(const Analysis *analysis, NyxType kind,
                             size_t *count);

/* The type index of Nil, or TYPED_NO_NODE if the program has none. */
size_t analysis_nil_type(const Analysis *analysis);

/* The indices of the nodes of `kind` that belong to the root at position
   `root` in the program's roots, in ascending order, and how many. A function
   root owns its body; a root that is a statement owns the statement. A nested
   function declaration is a root of its own and belongs to no other. */
const size_t *analysis_nodes_in(const Analysis *analysis, TypedNodeKind kind,
                                size_t root, size_t *count);

/* Releases what analyser_analyse or analysis_collect built. Safe on an
   Analysis that was zeroed or already freed. */
void analysis_free(Analysis *analysis);

static inline bool analysis_has(const Analysis *analysis,
                                AnalysisFeature feature)
{
    return (analysis->features & (uint32_t)feature) != 0;
}

/* True when some Store's generation can change while the program runs. Only a
   scope or a parallel pool is released before the program ends, so without one
   every handle's generation always matches and checking it is pointless. */
static inline bool analysis_generations_vary(const Analysis *analysis)
{
    return analysis_has(analysis, ANALYSIS_SCOPE);
}

/* clock_gettime, nanosleep, sysconf, and flockfile are POSIX, not C11. */
#define ANALYSIS_POSIX_FEATURES                                              \
    (ANALYSIS_NOW | ANALYSIS_SLEEP | ANALYSIS_PARALLEL | ANALYSIS_CORES |    \
     ANALYSIS_CHANNELS)

static inline bool analysis_needs_posix(const Analysis *analysis)
{
    return (analysis->features & ANALYSIS_POSIX_FEATURES) != 0;
}

#endif
