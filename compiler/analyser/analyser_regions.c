#include "analyser_internal.h"

#include "alloc.h"
#include "messages.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Escape analysis for scoped Stores.

   Every Store has a region depth: global Stores are 0, and each lexical
   `scope` nests one deeper. A function frame starts at depth 1, which is the
   level of everything that arrived through a parameter; a function's own
   scopes start at 2. A value is described by the range [lo, hi] of depths its
   pointers into Store memory may have. A value may be stored somewhere only if
   its `hi` does not exceed what the destination is guaranteed to outlive:
   `lo` for a container in a Store, the declaration depth for a variable.

   A struct parameter of at most eight fields has one slot per field, so a call
   is checked field by field: the summary says which field of which argument may
   be written into which field of another.

   A struct's fields and a union's alternatives are tracked separately while
   the value stays inline (a "shape"): reading `b.names` gives the facts of that
   field, not the join of every field. Anything else, including every value read
   out of a Store and every call argument or result, is one fact for the whole.

   The analysis is fail-closed and local. Variables are flow-insensitive: each
   keeps the join of everything assigned to it, and a function body is walked
   until no variable changes. Calls use a two-bit summary of the callee rather
   than its body: for each parameter, the set of parameters whose containers
   it may write that parameter's value into (`flow`), and the set whose value
   it may write into global memory (`to_global`), and the parameters its
   result may be derived from (`returned`). A call whose arguments could
   break one of those flows is rejected. Calls through function values assume
   every flow. */

#define REGION_NONE ((size_t)-1)

#define MAX_TRACKED_PARAMS 64

#define MAX_SHAPE 32

typedef struct {
    size_t hi;
    size_t lo; /* REGION_NONE when the value holds no pointer */
    uint64_t params; /* parameters the value may be derived from */
    size_t origin;   /* name of the Store behind `hi`, for diagnostics */
    SourcePos origin_pos;
    /* Per-field or per-alternative facts: `shape_count` Regions starting at
       `shape` in the thread's shape arena; zero when only the whole is known. */
    size_t shape;
    size_t shape_count;
} Region;

typedef struct {
    size_t name;
    /* Slots: a parameter has one, or one per field when it is a small struct.
       A slot is the unit of the masks below. The last slot is shared by
       whatever does not fit. */
    size_t param_count;
    unsigned char param_base[MAX_TRACKED_PARAMS];
    unsigned char param_slots[MAX_TRACKED_PARAMS];
    unsigned char slot_param[MAX_TRACKED_PARAMS];
    unsigned char slot_field[MAX_TRACKED_PARAMS];
    uint64_t flow[MAX_TRACKED_PARAMS];
    SourcePos flow_pos[MAX_TRACKED_PARAMS]; /* where each flow is retained */
    uint64_t to_global;
    SourcePos global_pos;
    uint64_t returned; /* slots the result may be derived from */
    bool returns_global; /* the result may point into memory of unknown depth */
} Summary;

typedef struct {
    size_t key;   /* declaring node * 2 + binding number */
    size_t depth; /* the depth the variable lives at */
    Region value;
} Var;

typedef struct {
    size_t name;
    bool is_store;
    size_t index;    /* Var slot, or the Store's region depth */
    uint64_t params; /* a Store parameter's own bit */
} Binding;

/* The region a parallel arm's result may point into, for `with var`. */
typedef struct {
    size_t node;
    Region value;
} ArmResult;

/* Shapes live in one append-only arena per analysis, owned by its Walker.
   Regions are small values passed by copy; a shape is referenced by index and
   never changed once written. Running out of memory only drops precision, but
   is also recorded so the analysis fails closed. */
typedef struct {
    List cells; /* Region */
    bool failed;
} ShapeArena;

typedef struct {
    const TypedProgram *program;
    ErrorState *errors;
    List vars;
    List bindings;
    List arm_results; /* ArmResult */
    Region *temps;
    size_t temp_count;
    List used_temps; /* temporaries holding a region in the current frame */
    Region receiver; /* the first parameter, for `self` */
    Summary *summaries;
    size_t summary_count;
    Summary *self; /* NULL while analysing top-level code */
    size_t base;   /* 0 for top-level code, 1 for a function */
    size_t depth;
    bool changed;
    bool flags_changed;
    bool failed;
    ShapeArena shapes;
} Walker;

static Region region_none(void)
{
    Region region = {0, REGION_NONE, 0, TYPED_NO_NODE, {0, 0, 0}, 0, 0};

    return region;
}

static Region region_at(size_t depth)
{
    Region region = {depth, depth, 0, TYPED_NO_NODE, {0, 0, 0}, 0, 0};

    return region;
}

static Region region_strip(Region region)
{
    region.shape = 0;
    region.shape_count = 0;
    return region;
}

static Region shape_kid(const ShapeArena *shapes, Region region, size_t index)
{
    if (index < region.shape_count) {
        return *(const Region *)list_get(&shapes->cells, region.shape + index);
    }
    return region_strip(region);
}

static size_t larger(size_t a, size_t b)
{
    return a > b ? a : b;
}

static size_t smaller(size_t a, size_t b)
{
    return a < b ? a : b;
}

/* Appends `count` regions to the arena and records where they start. An arena
   that cannot grow is marked failed, and the caller drops the shape. */
static bool append_shape(ShapeArena *shapes, const Region *kids, size_t count,
                         size_t *start)
{
    size_t i;

    *start = shapes->cells.length;
    for (i = 0; i < count; i++) {
        if (!list_push(&shapes->cells, &kids[i])) {
            shapes->failed = true;
            return false;
        }
    }
    return true;
}

static Region region_join(ShapeArena *shapes, Region left, Region right);

/* Both sides track the same members: join them member by member. */
static Region join_shapes(ShapeArena *shapes, Region result, Region left,
                          Region right)
{
    Region kids[MAX_SHAPE];
    size_t i;

    for (i = 0; i < left.shape_count; i++) {
        kids[i] = region_join(shapes, shape_kid(shapes, left, i),
                              shape_kid(shapes, right, i));
    }
    if (!append_shape(shapes, kids, left.shape_count, &result.shape)) {
        return region_strip(result);
    }
    result.shape_count = left.shape_count;
    return result;
}

static Region region_join(ShapeArena *shapes, Region left, Region right)
{
    Region result;

    if (left.lo == REGION_NONE || right.lo == REGION_NONE) {
        return left.lo == REGION_NONE ? right : left;
    }
    result = region_strip(left);
    result.hi = larger(left.hi, right.hi);
    result.lo = smaller(left.lo, right.lo);
    result.params = left.params | right.params;
    if (right.hi > left.hi) {
        result.origin = right.origin;
        result.origin_pos = right.origin_pos;
    }
    if (left.shape_count != 0 && left.shape_count == right.shape_count) {
        return join_shapes(shapes, result, left, right);
    }
    return result;
}

static bool region_equal(ShapeArena *shapes, Region left, Region right)
{
    size_t i;

    if (left.lo != right.lo) {
        return false;
    }
    if (left.lo == REGION_NONE) {
        return true;
    }
    if (left.hi != right.hi || left.params != right.params ||
        left.shape_count != right.shape_count) {
        return false;
    }
    for (i = 0; i < left.shape_count; i++) {
        if (!region_equal(shapes, shape_kid(shapes, left, i), shape_kid(shapes, right, i))) {
            return false;
        }
    }
    return true;
}

/* A value built from separately tracked members. The whole is their join. */
static Region make_shape(ShapeArena *shapes, const Region *kids, size_t count)
{
    Region whole = region_none();
    size_t i;

    for (i = 0; i < count; i++) {
        whole = region_join(shapes, whole, region_strip(kids[i]));
    }
    if (whole.lo == REGION_NONE || count == 0 || count > MAX_SHAPE) {
        return whole;
    }
    if (!append_shape(shapes, kids, count, &whole.shape)) {
        return region_strip(whole);
    }
    whole.shape_count = count;
    return whole;
}

/* `base` with `value` written at the member path `path`. A base that does not
   track that path takes the value as a whole. */
static Region region_patch(ShapeArena *shapes, Region base, const size_t *path, size_t length,
                           Region value)
{
    Region kids[MAX_SHAPE];
    size_t i;

    if (length == 0) {
        return region_join(shapes, base, value);
    }
    if (base.lo == REGION_NONE) {
        return region_strip(value);
    }
    if (base.shape_count == 0 || path[0] >= base.shape_count) {
        return region_join(shapes, base, region_strip(value));
    }
    for (i = 0; i < base.shape_count; i++) {
        kids[i] = shape_kid(shapes, base, i);
    }
    kids[path[0]] = region_patch(shapes, kids[path[0]], path + 1, length - 1, value);
    return make_shape(shapes, kids, base.shape_count);
}

/* Copies a region and the shapes under it into `to`, so the arena can be
   rebuilt from the regions still in use. */
static Region region_clone(const ShapeArena *shapes, Region region, List *to,
                           bool *ok)
{
    Region kids[MAX_SHAPE];
    size_t i;

    if (region.shape_count == 0) {
        return region;
    }
    for (i = 0; i < region.shape_count; i++) {
        kids[i] = region_clone(shapes, shape_kid(shapes, region, i), to, ok);
    }
    region.shape = to->length;
    for (i = 0; i < region.shape_count; i++) {
        if (!list_push(to, &kids[i])) {
            *ok = false;
            return region_strip(region);
        }
    }
    return region;
}

/* Types that can never hold a pointer into Store memory carry no region. */
static bool type_holds_pointers(const TypedProgram *program, size_t type_index)
{
    static const NyxType pointer_free[] = {
        NYX_TYPE_UNKNOWN, NYX_TYPE_INT32, NYX_TYPE_BOOL,
        NYX_TYPE_BYTE, NYX_TYPE_NIL, NYX_TYPE_FUNCTION,
        NYX_TYPE_STRING, NYX_TYPE_RANGE, NYX_TYPE_STRING_LITERAL,
        NYX_TYPE_ERROR, NYX_TYPE_INT8, NYX_TYPE_INT16,
        NYX_TYPE_INT64, NYX_TYPE_UINT16, NYX_TYPE_UINT32,
        NYX_TYPE_UINT64, NYX_TYPE_FLOAT32, NYX_TYPE_FLOAT64,
    };
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL) {
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(pointer_free); i++) {
        if (pointer_free[i] == type->kind) {
            return false;
        }
    }
    return true;
}

static size_t struct_field_count(const TypedProgram *program, size_t type_index)
{
    const TypedType *type = typed_program_type(program, type_index);
    const TypedNode *declaration;

    if (type == NULL || type->kind != NYX_TYPE_STRUCT ||
        type->declaration_node == TYPED_NO_NODE) {
        return 0;
    }
    declaration = typed_program_node(program, type->declaration_node);
    return declaration == NULL ? 0 : declaration->children_count;
}

static size_t struct_field_index(const TypedProgram *program, size_t type_index,
                                 size_t name_index)
{
    const TypedType *type = typed_program_type(program, type_index);
    const TypedNode *declaration;
    size_t i;

    if (type == NULL || type->kind != NYX_TYPE_STRUCT ||
        type->declaration_node == TYPED_NO_NODE) {
        return TYPED_NO_NODE;
    }
    declaration = typed_program_node(program, type->declaration_node);
    for (i = 0; declaration != NULL && i < declaration->children_count; i++) {
        const TypedNode *field = typed_program_node(
            program, *(const size_t *)list_get(&program->children,
                                               declaration->children_start + i));

        if (field != NULL && field->kind == TYPED_FIELD &&
            field->name_index == name_index) {
            return i;
        }
    }
    return TYPED_NO_NODE;
}

static const char *symbol_name(const TypedProgram *program, size_t index)
{
    const char *name = typed_program_symbol_text(program, index);

    return name == NULL ? "?" : name;
}

/* Methods and generic instances carry generated names; do not show those. */
static const char *callee_name(const TypedProgram *program, size_t index)
{
    const char *name = symbol_name(program, index);

    return strncmp(name, "nyx_", 4) == 0 ? MESSAGE_FRAGMENT_GENERATED_CALLEE : name;
}

/* "'name'" for a known Store, otherwise a generic phrase. */
static void describe_origin(const TypedProgram *program, size_t origin,
                            char *text, size_t capacity)
{
    if (origin == TYPED_NO_NODE) {
        snprintf(text, capacity, MESSAGE_FRAGMENT_INNER_SCOPE);
    } else {
        snprintf(text, capacity, MESSAGE_FRAGMENT_STORE_PREFIX "%s'",
                 symbol_name(program, origin));
    }
}

/* A step that reaches into a value rather than naming a variable. */
static bool is_projection(TypedNodeKind kind)
{
    static const TypedNodeKind projections[] = {
        TYPED_MEMBER_ACCESS, TYPED_HANDLE_READ, TYPED_LIST_GET,
        TYPED_SLICE_GET,     TYPED_RECORD_GET,  TYPED_TABLE_GET,
    };
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(projections); i++) {
        if (projections[i] == kind) {
            return true;
        }
    }
    return false;
}

/* The variable or Store a write lands in, for diagnostics. */
static const char *destination_name(const TypedProgram *program, size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    while (node != NULL && is_projection(node->kind)) {
        node = typed_program_node(program, node->left_child);
    }
    if (node != NULL && node->kind == TYPED_IDENTIFIER && !node->is_self) {
        return symbol_name(program, node->name_index);
    }
    return MESSAGE_FRAGMENT_LONGER_LIVED;
}

static void fail_oom(Walker *walker)
{
    if (!walker->failed) {
        error_report(walker->errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
    }
    walker->failed = true;
}

static size_t find_var(Walker *walker, size_t key, size_t depth)
{
    size_t i;
    Var var;

    for (i = 0; i < walker->vars.length; i++) {
        const Var *existing = list_get(&walker->vars, i);

        if (existing->key == key) {
            return i;
        }
    }
    var.key = key;
    var.depth = depth;
    var.value = region_none();
    if (!list_push(&walker->vars, &var)) {
        fail_oom(walker);
        return TYPED_NO_NODE;
    }
    return walker->vars.length - 1;
}

static bool bind(Walker *walker, size_t name, bool is_store, size_t index,
                 uint64_t params)
{
    Binding binding;

    binding.name = name;
    binding.is_store = is_store;
    binding.index = index;
    binding.params = params;
    if (!list_push(&walker->bindings, &binding)) {
        fail_oom(walker);
        return false;
    }
    return true;
}

static const Binding *lookup(const Walker *walker, size_t name, bool is_store)
{
    size_t i = walker->bindings.length;

    while (i > 0) {
        const Binding *binding = list_get(&walker->bindings, --i);

        if (binding->name == name && binding->is_store == is_store) {
            return binding;
        }
    }
    return NULL;
}

static bool add_mask(Walker *walker, uint64_t *mask, uint64_t bits)
{
    if ((*mask | bits) != *mask) {
        *mask |= bits;
        walker->flags_changed = true;
        return true;
    }
    return false;
}

/* Records that values derived from `sources` may be written into memory that
   `sinks` points into, by the operation at `pos`. Only meaningful inside a
   function. */
static void record_flow(Walker *walker, uint64_t sources, uint64_t sinks,
                        SourcePos pos)
{
    size_t i;

    if (walker->self == NULL || sinks == 0) {
        return;
    }
    for (i = 0; i < MAX_TRACKED_PARAMS; i++) {
        if (((sources >> i) & 1) &&
            add_mask(walker, &walker->self->flow[i], sinks)) {
            walker->self->flow_pos[i] = pos;
        }
    }
}

static void record_global(Walker *walker, uint64_t sources, SourcePos pos)
{
    if (walker->self != NULL &&
        add_mask(walker, &walker->self->to_global, sources)) {
        walker->self->global_pos = pos;
    }
}

/* The second line of a diagnostic: where the offending value was allocated. */
static void note_origin(Walker *walker, Region value)
{
    if (value.origin != TYPED_NO_NODE) {
        error_report_at(walker->errors, value.origin_pos,
                        MESSAGE_ANALYSER_NOTE_ALLOCATED,
                        symbol_name(walker->program, value.origin));
    }
}

/* Records `value` into variable slot `slot`, rejecting it when the variable
   would outlive the value. With a member path, only that member of the
   variable changes. */
static void assign_var_at(Walker *walker, size_t slot, Region value,
                          const TypedNode *node, const char *name,
                          const size_t *path, size_t path_length)
{
    Var *var;
    Region joined;

    if (slot == TYPED_NO_NODE || value.lo == REGION_NONE) {
        return;
    }
    var = (Var *)list_get(&walker->vars, slot);
    if (value.hi > var->depth) {
        char origin[NYX_IDENTIFIER_MAX + 16];

        describe_origin(walker->program, value.origin, origin, sizeof(origin));
        error_report_at(walker->errors, node->pos, MESSAGE_ANALYSER_ESCAPE_ASSIGN,
                        origin, name);
        note_origin(walker, value);
        walker->failed = true;
        return;
    }
    if (path != NULL && path_length == 0) {
        /* A write to a member the analysis cannot place: its own shape does
           not describe the variable. */
        value = region_strip(value);
    }
    joined = region_patch(&walker->shapes, var->value, path, path_length, value);
    if (!region_equal(&walker->shapes, joined, var->value)) {
        var->value = joined;
        walker->changed = true;
    }
}

static void assign_var(Walker *walker, size_t slot, Region value,
                       const TypedNode *node, const char *name)
{
    assign_var_at(walker, slot, value, node, name, NULL, 0);
}

/* A value is written into memory that is guaranteed to outlive depth `floor`.
   Inside a function, a parameter-derived value may be written into a
   parameter's container, whose real depth the caller decides, or into memory
   that might be global. Those flows go into the summary. */
static void check_write(Walker *walker, Region container, Region value,
                        const TypedNode *node, const char *destination)
{
    char origin[NYX_IDENTIFIER_MAX + 16];

    size_t floor = container.lo == REGION_NONE ? 0 : container.lo;

    if (value.lo == REGION_NONE) {
        return;
    }
    if (value.hi <= floor) {
        record_flow(walker, value.params, container.params, node->pos);
        return;
    }
    if (walker->base == 1 && floor == 0 && value.hi == 1) {
        record_flow(walker, value.params, container.params, node->pos);
        record_global(walker, value.params, node->pos);
        return;
    }
    describe_origin(walker->program, value.origin, origin, sizeof(origin));
    error_report_at(walker->errors, node->pos, MESSAGE_ANALYSER_ESCAPE_WRITE,
                    origin, destination);
    note_origin(walker, value);
    walker->failed = true;
}

static Region store_region(const Walker *walker, const TypedNode *node)
{
    const Binding *binding;
    Region region;

    if (node->store_id != TYPED_NO_NODE) {
        return region_at(0);
    }
    binding = lookup(walker, node->name_index, true);
    if (binding == NULL) {
        return region_at(0);
    }
    region = region_at(binding->index);
    region.params = binding->params;
    region.origin = node->name_index;
    region.origin_pos = node->pos;
    return region;
}

static Region visit(Walker *walker, size_t index);

/* The variable a chain of member accesses starts from, or NULL. */
static const TypedNode *member_root(const TypedProgram *program,
                                    const TypedNode *node)
{
    while (node != NULL && node->kind == TYPED_MEMBER_ACCESS) {
        node = typed_program_node(program, node->left_child);
    }
    return node;
}

/* A fixed-length list or a record lives inside its variable, so a write into
   it changes the variable. Returns the variable's binding, or NULL when the
   receiver is not such a local. */
static const Binding *by_value_receiver(const Walker *walker, size_t index)
{
    NyxType kind = typed_program_node_kind(walker->program, index);
    const TypedNode *root = member_root(walker->program,
                                        typed_program_node(walker->program, index));

    if ((kind != NYX_TYPE_FIXED_LIST && kind != NYX_TYPE_RECORD) ||
        root == NULL || root->kind != TYPED_IDENTIFIER || root->is_self) {
        return NULL;
    }
    return lookup(walker, root->name_index, false);
}

static Region visit_children(Walker *walker, const TypedNode *node)
{
    Region result = region_none();
    size_t i;

    if (node->left_child != TYPED_NO_NODE) {
        result = region_join(&walker->shapes, result, visit(walker, node->left_child));
    }
    if (node->right_child != TYPED_NO_NODE) {
        result = region_join(&walker->shapes, result, visit(walker, node->right_child));
    }
    for (i = 0; i < node->children_count && !walker->failed; i++) {
        size_t child = *(const size_t *)list_get(&walker->program->children,
                                                 node->children_start + i);

        result = region_join(&walker->shapes, result, visit(walker, child));
    }
    return result;
}

/* A read from a container yields values no deeper than the container, but
   nothing is known about how shallow they are. */
static Region contents_of(Region container)
{
    Region result = region_strip(container);

    if (container.lo == REGION_NONE) {
        return region_none();
    }
    result.lo = 0;
    return result;
}

static const Summary *find_summary(const Walker *walker, size_t name)
{
    size_t i;

    for (i = 0; i < walker->summary_count; i++) {
        if (walker->summaries[i].name == name) {
            return &walker->summaries[i];
        }
    }
    return NULL;
}

/* A call that may keep one argument beyond the lifetime of its Store. The
   notes point at the allocation and, when the callee's summary knows it, at the
   operation that retains the argument. */
static void report_call(Walker *walker, const TypedNode *node, Region source,
                        const Summary *summary, size_t source_index,
                        bool global)
{
    char origin[NYX_IDENTIFIER_MAX + 16];
    const char *callee = callee_name(walker->program, node->name_index);

    describe_origin(walker->program, source.origin, origin, sizeof(origin));
    error_report_at(walker->errors, node->pos, MESSAGE_ANALYSER_ESCAPE_CALL,
                    callee, origin);
    note_origin(walker, source);
    if (summary != NULL) {
        error_report_at(walker->errors,
                        global ? summary->global_pos
                               : summary->flow_pos[source_index],
                        MESSAGE_ANALYSER_NOTE_RETAINED, callee);
    }
    walker->failed = true;
}

/* Checks one flow of argument `source` into the container of `sink`, and
   passes it on to this function's own summary when both come from parameters. */
static void check_flow(Walker *walker, const TypedNode *node, Region source,
                       Region sink, const Summary *summary, size_t source_index)
{
    if (source.lo == REGION_NONE || sink.lo == REGION_NONE) {
        return;
    }
    if (source.hi <= sink.lo) {
        record_flow(walker, source.params, sink.params, node->pos);
        return;
    }
    if (walker->base == 1 && source.hi == 1 && sink.lo == 0) {
        record_flow(walker, source.params, sink.params, node->pos);
        record_global(walker, source.params, node->pos);
        return;
    }
    report_call(walker, node, source, summary, source_index, false);
}

static void check_global_flow(Walker *walker, const TypedNode *node,
                              Region source, const Summary *summary)
{
    if (source.lo == REGION_NONE || source.hi == 0) {
        return;
    }
    if (walker->base == 1 && source.hi == 1) {
        record_global(walker, source.params, node->pos);
        return;
    }
    report_call(walker, node, source, summary, 0, true);
}

static size_t param_index(size_t index)
{
    return index < MAX_TRACKED_PARAMS ? index : MAX_TRACKED_PARAMS - 1;
}

#define MAX_PARAM_FIELDS 8

/* How many slots a parameter takes: one per field for a small struct, else one,
   and one when the rest would not fit. */
static size_t parameter_slot_count(const TypedProgram *program,
                                   const TypedNode *parameter, size_t next)
{
    size_t fields = struct_field_count(program, parameter->type_index);
    size_t count = fields >= 2 && fields <= MAX_PARAM_FIELDS ? fields : 1;

    return next + count > MAX_TRACKED_PARAMS - 1 ? 1 : count;
}

/* Assigns each parameter of `function` its slots. */
static void layout_summary(const TypedProgram *program,
                           const TypedNode *function, Summary *summary)
{
    size_t next = 0;
    size_t i;

    summary->param_count = function->children_count;
    for (i = 0; i < MAX_TRACKED_PARAMS; i++) {
        summary->param_base[i] = MAX_TRACKED_PARAMS - 1;
        summary->param_slots[i] = 1;
    }
    for (i = 0; i < function->children_count && next < MAX_TRACKED_PARAMS - 1;
         i++) {
        size_t count = parameter_slot_count(
            program,
            typed_program_node(program,
                               *(const size_t *)list_get(
                                   &program->children,
                                   function->children_start + i)),
            next);
        size_t f;

        summary->param_base[i] = (unsigned char)next;
        summary->param_slots[i] = (unsigned char)count;
        for (f = 0; f < count; f++) {
            summary->slot_param[next + f] = (unsigned char)i;
            summary->slot_field[next + f] = (unsigned char)f;
        }
        next += count;
    }
    summary->slot_param[MAX_TRACKED_PARAMS - 1] = MAX_TRACKED_PARAMS - 1;
}

static uint64_t slot_mask(const Summary *summary, size_t param)
{
    size_t index = param_index(param);
    uint64_t mask = 0;
    size_t f;

    for (f = 0; f < summary->param_slots[index]; f++) {
        mask |= (uint64_t)1 << (summary->param_base[index] + f);
    }
    return mask;
}

/* What a parameter holds on entry: depth one, derived from its own slots. */
static Region param_region(ShapeArena *shapes, const Summary *summary,
                           size_t param)
{
    size_t index = param_index(param);
    size_t count = summary->param_slots[index];
    Region kids[MAX_PARAM_FIELDS];
    Region whole = region_at(1);
    size_t f;

    if (count < 2) {
        whole.params = slot_mask(summary, param);
        return whole;
    }
    for (f = 0; f < count; f++) {
        kids[f] = region_at(1);
        kids[f].params = (uint64_t)1 << (summary->param_base[index] + f);
    }
    return make_shape(shapes, kids, count);
}

/* The part of an argument that parameter slot `field` stands for. */
static Region argument_part(const ShapeArena *shapes, Region argument,
                            size_t field, size_t slots)
{
    if (slots >= 2 && argument.shape_count == slots) {
        return shape_kid(shapes, argument, field);
    }
    return region_strip(argument);
}

/* A walk over the parameter slots of the arguments a call passes: each
   argument, then each field slot of it. */
typedef struct {
    const Summary *summary;
    size_t count;   /* arguments to walk */
    size_t argument;
    size_t field;
    size_t slot;    /* the slot the cursor is on */
    size_t slots;   /* slots of the current argument */
} SlotCursor;

static SlotCursor slot_cursor(const Summary *summary, size_t count)
{
    SlotCursor cursor = {summary, count < summary->param_count
                                      ? count : summary->param_count,
                         0, 0, 0, 0};

    cursor.slots = summary->param_slots[param_index(0)];
    cursor.field = (size_t)-1;
    return cursor;
}

static bool slot_next(SlotCursor *cursor)
{
    size_t index;

    if (++cursor->field >= cursor->slots) {
        cursor->argument++;
        cursor->field = 0;
        cursor->slots = cursor->summary->param_slots[param_index(
            cursor->argument)];
    }
    if (cursor->argument >= cursor->count) {
        return false;
    }
    index = param_index(cursor->argument);
    cursor->slot = cursor->summary->param_base[index] + cursor->field;
    return true;
}

static Region cursor_part(const Walker *walker, const SlotCursor *cursor,
                          const Region *arguments)
{
    return argument_part(&walker->shapes, arguments[cursor->argument],
                         cursor->field, cursor->slots);
}

/* The flows the callee's summary promises, checked against these arguments. */
static void check_call_contract_source(Walker *walker, const TypedNode *node,
                                       const Summary *summary,
                                       const Region *arguments, size_t count)
{
    SlotCursor from = slot_cursor(summary, count);

    while (!walker->failed && slot_next(&from)) {
        Region source = cursor_part(walker, &from, arguments);
        SlotCursor to = slot_cursor(summary, count);

        while (!walker->failed && slot_next(&to)) {
            if ((summary->flow[from.slot] >> to.slot) & 1) {
                check_flow(walker, node, source,
                           cursor_part(walker, &to, arguments), summary,
                           from.slot);
            }
        }
        if (!walker->failed && ((summary->to_global >> from.slot) & 1)) {
            check_global_flow(walker, node, source, summary);
        }
    }
}

/* Every argument may go into every other; a direct call to a function with no
   summary may also reach global memory. */
static void check_unknown_callee(Walker *walker, const TypedNode *node,
                                 bool indirect, const Region *arguments,
                                 size_t count)
{
    size_t i, j;

    for (i = 0; i < count && !walker->failed; i++) {
        for (j = 0; j < count && !walker->failed; j++) {
            check_flow(walker, node, region_strip(arguments[i]),
                       region_strip(arguments[j]), NULL, 0);
        }
        if (!indirect && !walker->failed) {
            check_global_flow(walker, node, region_strip(arguments[i]), NULL);
        }
    }
}

/* A call through a function value assumes the callee may write any argument
   into any other, but never into global memory: a function that could is
   rejected where it becomes a value. A callee with no summary is assumed to
   do both. */
static void check_call_contract(Walker *walker, const TypedNode *node,
                                const Summary *summary, bool indirect,
                                const Region *arguments, size_t count)
{
    if (summary == NULL) {
        check_unknown_callee(walker, node, indirect, arguments, count);
    } else {
        check_call_contract_source(walker, node, summary, arguments, count);
    }
}

/* What argument `i` contributes to the call's result: all of it when nothing
   is known of the callee, else the parts the callee derives its result from. */
static Region argument_result(Walker *walker, const Summary *summary,
                              bool indirect, const Region *arguments, size_t i)
{
    Region result = region_none();
    size_t slots, f;

    if (indirect || summary == NULL || i >= summary->param_count) {
        return region_strip(arguments[i]);
    }
    slots = summary->param_slots[param_index(i)];
    for (f = 0; f < slots; f++) {
        if ((summary->returned >> (summary->param_base[param_index(i)] + f)) &
            1) {
            result = region_join(&walker->shapes, result,
                                 argument_part(&walker->shapes, arguments[i],
                                               f, slots));
        }
    }
    return result;
}

/* The result is reachable from the arguments the callee derives it from, and
   from global memory only if the callee may return such a value. */
static Region call_result(Walker *walker, const Summary *summary, bool indirect,
                          const Region *arguments, size_t count)
{
    Region result = region_none();
    size_t i;

    for (i = 0; i < count; i++) {
        result = region_join(&walker->shapes, result,
                             argument_result(walker, summary, indirect,
                                             arguments, i));
    }
    if (indirect || summary == NULL || summary->returns_global) {
        result = region_join(&walker->shapes, result, region_at(0));
    }
    return result;
}

static Region visit_call(Walker *walker, size_t index)
{
    const TypedNode *node = typed_program_node(walker->program, index);
    size_t count = node->children_count;
    Region *arguments = nyx_malloc((count + 1) * sizeof(*arguments));
    Region result = region_none();
    bool indirect;
    const Summary *summary;
    size_t i;

    if (arguments == NULL) {
        fail_oom(walker);
        return result;
    }
    for (i = 0; i < count && !walker->failed; i++) {
        arguments[i] = visit(walker, *(const size_t *)list_get(
                                         &walker->program->children,
                                         node->children_start + i));
    }
    if (!walker->failed) {
        indirect = lookup(walker, node->name_index, false) != NULL;
        summary = indirect ? NULL : find_summary(walker, node->name_index);
        check_call_contract(walker, node, summary, indirect, arguments, count);
        result = call_result(walker, summary, indirect, arguments, count);
    }
    free(arguments);
    return result;
}

static Region visit_declaration(Walker *walker, size_t index, size_t key,
                                size_t name, Region value)
{
    const TypedNode *node = typed_program_node(walker->program, index);
    size_t slot = find_var(walker, key, walker->depth);

    if (slot != TYPED_NO_NODE) {
        assign_var(walker, slot, value, node,
                   symbol_name(walker->program, name));
        if (!walker->failed) {
            bind(walker, name, false, slot, 0);
        }
    }
    return region_none();
}

static bool visit_inner_region_none(Region *first, const TypedNode *node, Walker *walker, const TypedType *type, size_t *i, Region (*kids)[32])
{
    (*first) = node->left_child == TYPED_NO_NODE
                ? region_none()
                : visit(walker, node->left_child);
    if (type == NULL || type->kind != NYX_TYPE_UNION ||
        node->integer_value < 0 ||
        (size_t)node->integer_value >= type->child_count ||
        type->child_count > MAX_SHAPE || (*first).lo == REGION_NONE) {
        return false;
    }
    for ((*i) = 0; (*i) < type->child_count; (*i)++) {
        (*kids)[(*i)] = region_none();
    }
    return true;
}

static void visit_inner_add_mask(Walker *walker, Region *first, const TypedProgram *program, const TypedNode *node)
{
    if (!walker->failed && walker->self != NULL) {
        add_mask(walker, &walker->self->returned, (*first).params);
        if ((*first).lo == 0 && !walker->self->returns_global) {
            walker->self->returns_global = true;
            walker->flags_changed = true;
        }
    }
    if (!walker->failed && (*first).lo != REGION_NONE &&
        (*first).hi > walker->base) {
        char origin[NYX_IDENTIFIER_MAX + 16];

        describe_origin(program, (*first).origin, origin, sizeof(origin));
        error_report_at(walker->errors, node->pos,
                        MESSAGE_ANALYSER_ESCAPE_RETURN, origin);
        note_origin(walker, (*first));
        walker->failed = true;
    }
}

static void visit_inner_length(const TypedProgram *program, const TypedNode *node, size_t (*reversed)[32], size_t *i, size_t (*path)[32], size_t *length)
{
    (*length) = 0;
    bool known = true;
    const TypedNode *step = typed_program_node(program, node->left_child);

    while (step != NULL && step->kind == TYPED_MEMBER_ACCESS) {
        const TypedNode *receiver = typed_program_node(
            program, step->left_child);
        size_t field = receiver == NULL
                           ? TYPED_NO_NODE
                           : struct_field_index(program,
                                                receiver->type_index,
                                                step->name_index);

        if (field == TYPED_NO_NODE || (*length) == MAX_SHAPE) {
            known = false;
            break;
        }
        (*reversed)[(*length)++] = field;
        step = receiver;
    }
    if (!known) {
        (*length) = 0;
    }
    for ((*i) = 0; (*i) < (*length); (*i)++) {
        (*path)[(*i)] = (*reversed)[(*length) - 1 - (*i)];
    }
}

static bool visit_inner_visit(Region *first, Walker *walker, const TypedNode *node, size_t *i, const TypedProgram *program)
{
    (*first) = visit(walker, node->left_child);
    if (node->right_child != TYPED_NO_NODE) {
        visit(walker, node->right_child);
    }
    for ((*i) = 0; (*i) < node->children_count && !walker->failed; (*i)++) {
        visit(walker, *(const size_t *)list_get(&program->children,
                                                node->children_start + (*i)));
    }
    {
        /* An inline Array or Record holds its elements itself, so what is
           read from it is among what was written into the variable. */
        const TypedNode *container = typed_program_node(program,
                                                        node->left_child);
        const TypedType *container_type =
            container == NULL ? NULL
                              : typed_program_type(program,
                                                   container->type_index);

        if (container_type != NULL &&
            (container_type->kind == NYX_TYPE_FIXED_LIST ||
             container_type->kind == NYX_TYPE_RECORD)) {
            return false;
        }
    }
    return true;
}

static bool visit_inner_source(const TypedProgram *program, const TypedNode *node, Region *first, Walker *walker)
{
    const TypedNode *source = typed_program_node(program, node->left_child);
    const TypedType *type = source == NULL ? NULL
                            : typed_program_type(program, source->type_index);

    (*first) = visit(walker, node->left_child);
    if (type == NULL || type->kind != NYX_TYPE_UNION ||
        node->integer_value < 0 ||
        (*first).shape_count != type->child_count ||
        (size_t)node->integer_value >= (*first).shape_count) {
        return false;
    }
    return true;
}

static bool visit_inner_iterable(Walker *walker, const TypedNode *node, size_t *mark, size_t index, const TypedProgram *program)
{
    Region iterable = visit(walker, node->left_child);
    Region element = contents_of(iterable);
    size_t slot;

    (*mark) = walker->bindings.length;
    if (walker->failed) {
        return false;
    }
    slot = find_var(walker, index * 2, walker->depth);
    if (slot == TYPED_NO_NODE) {
        return false;
    }
    assign_var(walker, slot, element, node,
               symbol_name(program, node->name_index));
    bind(walker, node->name_index, false, slot, 0);
    if (node->second_name_index != TYPED_NO_NODE && !walker->failed) {
        slot = find_var(walker, index * 2 + 1, walker->depth);
        if (slot == TYPED_NO_NODE) {
            return false;
        }
        assign_var(walker, slot, element, node,
               symbol_name(program, node->name_index));
        bind(walker, node->second_name_index, false, slot, 0);
    }
    if (!walker->failed) {
        visit(walker, node->right_child);
    }
    return true;
}

static bool visit_inner_receiver(const TypedProgram *program, const TypedNode *node, Region *first, Walker *walker, size_t *field)
{
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    (*field) = receiver == NULL
                       ? TYPED_NO_NODE
                       : struct_field_index(program, receiver->type_index,
                                            node->name_index);

    (*first) = visit(walker, node->left_child);
    if ((*field) == TYPED_NO_NODE || (*first).shape_count == 0 ||
        (*first).shape_count != struct_field_count(program,
                                                receiver->type_index)) {
        return false;
    }
    return true;
}

static void visit_inner_owner(Walker *walker, const TypedNode *node, Region *first, size_t *i, const TypedProgram *program, Region *second)
{
    const Binding *owner = by_value_receiver(walker, node->left_child);

    (*first) = visit(walker, node->left_child);
    for ((*i) = 0; (*i) < node->children_count && !walker->failed; (*i)++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + (*i));

        (*second) = visit(walker, child);
        if (walker->failed) {
            break;
        }
        if (owner != NULL) {
            assign_var(walker, owner->index, (*second), node,
                       symbol_name(program, owner->name));
        } else {
            check_write(walker, (*first), (*second), node,
                        destination_name(program, node->left_child));
        }
    }
}

static bool visit_inner_region_none13(Walker *walker, const TypedNode **target, const TypedProgram *program)
{
    if (walker->failed) {
        return false;
    }
    while ((*target) != NULL && (*target)->kind == TYPED_MEMBER_ACCESS) {
        (*target) = typed_program_node(program, (*target)->left_child);
    }
    return true;
}

static void visit_inner_joined(Walker *walker, size_t *temp, Region *first)
{
    {
        Region joined = region_join(&walker->shapes, walker->temps[(*temp)], (*first));

        if (!region_equal(&walker->shapes, joined, walker->temps[(*temp)])) {
            if (walker->temps[(*temp)].lo == REGION_NONE &&
                !list_push(&walker->used_temps, temp)) {
                fail_oom(walker);
            }
            walker->temps[(*temp)] = joined;
            walker->changed = true;
        }
    }
}

static void visit_inner_visit16(Region *first, Walker *walker, const TypedNode *node, Region *second, const TypedProgram *program)
{
    (*first) = visit(walker, node->left_child);
    (*second) = visit(walker, node->right_child);
    if (!walker->failed && node->kind != TYPED_BUILDER_APPEND &&
        node->kind != TYPED_BUILDER_APPEND_BYTE) {
        check_write(walker, (*first), (*second), node,
                    destination_name(program, node->left_child));
    }
}

/* Each kind of node has a visitor that returns the region of its value. The
   bodies are the cases of the walk; the table below picks one by kind, and a
   kind without one just visits its children. */
typedef Region (*NodeVisitor)(Walker *walker, size_t index,
                              const TypedNode *node);

static Region visit_block(Walker *walker, size_t index, const TypedNode *node)
{
    size_t mark = walker->bindings.length;

    (void)index;
    visit_children(walker, node);
    walker->bindings.length = mark;
    return region_none();
}

static Region visit_scope(Walker *walker, size_t index, const TypedNode *node)
{
    size_t depth = walker->depth + 1;
    size_t mark;

    (void)index;
    visit(walker, node->left_child);
    mark = walker->bindings.length;
    walker->depth = depth;
    if (!bind(walker, node->name_index, true, depth, 0)) {
        return region_none();
    }
    visit(walker, node->right_child);
    walker->depth = depth - 1;
    walker->bindings.length = mark;
    return region_none();
}

/* A parallel block with a pool is a scope: the pool lives one level deeper,
   for its arms and its then section. */
static Region visit_parallel(Walker *walker, size_t index,
                             const TypedNode *node)
{
    const TypedProgram *program = walker->program;
    bool pool = node->integer_value == 1;
    size_t depth = walker->depth + 1;
    size_t mark = walker->bindings.length;
    size_t i;

    (void)index;
    if (pool) {
        visit(walker, node->left_child);
        walker->depth = depth;
        if (!bind(walker, node->name_index, true, depth, 0)) {
            return region_none();
        }
    }
    if (node->setup_block != TYPED_NO_NODE) {
        /* The declarations bind names the arms and the then section use. */
        visit_children(walker, typed_program_node(program, node->setup_block));
    }
    for (i = 0; i < node->children_count; i++) {
        visit(walker, *(const size_t *)list_get(&program->children,
                                                node->children_start + i));
    }
    if (node->right_child != TYPED_NO_NODE) {
        visit(walker, node->right_child);
    }
    if (pool) {
        walker->depth = depth - 1;
    }
    walker->bindings.length = mark;
    return region_none();
}

/* Remembers the region an arm produced, for the result nodes that read it. */
static void record_arm_result(Walker *walker, const ArmResult *result)
{
    size_t k;

    for (k = 0; k < walker->arm_results.length; k++) {
        ArmResult *known = (ArmResult *)list_get(&walker->arm_results, k);

        if (known->node == result->node) {
            known->value = result->value;
            return;
        }
    }
    if (!list_push(&walker->arm_results, result)) {
        fail_oom(walker);
    }
}

static Region visit_parallel_arm(Walker *walker, size_t index,
                                 const TypedNode *node)
{
    const TypedProgram *program = walker->program;
    ArmResult result;
    size_t k;

    for (k = 0; k < node->children_count; k++) {
        visit(walker, *(const size_t *)list_get(&program->children,
                                                node->children_start + k));
    }
    result.node = index;
    result.value = visit(walker, node->left_child);
    if (!walker->failed && node->integer_value == 1) {
        const Binding *target = lookup(walker, node->name_index, false);

        if (target != NULL) {
            assign_var(walker, target->index, result.value, node,
                       symbol_name(program, node->name_index));
        }
    }
    record_arm_result(walker, &result);
    return region_none();
}

static Region visit_parallel_result(Walker *walker, size_t index,
                                    const TypedNode *node)
{
    size_t k;

    (void)index;
    for (k = 0; k < walker->arm_results.length; k++) {
        const ArmResult *known = list_get(&walker->arm_results, k);

        if (known->node == (size_t)node->integer_value_u) {
            return known->value;
        }
    }
    return region_at(0);
}

/* A function used as a value is rejected if calling it could write into global
   memory, because a call through the value is assumed not to. */
static Region visit_function_value(Walker *walker, const TypedNode *node)
{
    const Summary *callee = find_summary(walker, node->name_index);

    if (callee != NULL && callee->to_global != 0) {
        error_report_at(walker->errors, node->pos,
                        MESSAGE_ANALYSER_ESCAPE_FUNCTION_VALUE,
                        callee_name(walker->program, node->name_index));
        walker->failed = true;
    }
    return region_none();
}

static Region visit_identifier(Walker *walker, size_t index,
                               const TypedNode *node)
{
    NyxType kind = typed_program_node_kind(walker->program, index);
    const Binding *binding = lookup(walker, node->name_index, false);

    if (kind == NYX_TYPE_STORE) {
        return store_region(walker, node);
    }
    if (kind == NYX_TYPE_FUNCTION && binding == NULL) {
        return visit_function_value(walker, node);
    }
    if (node->is_self) {
        return walker->receiver;
    }
    if (binding == NULL) {
        return region_at(0);
    }
    return ((const Var *)list_get(&walker->vars, binding->index))->value;
}

static Region visit_declaration_node(Walker *walker, size_t index,
                                     const TypedNode *node)
{
    Region first = node->left_child == TYPED_NO_NODE
                       ? region_none()
                       : visit(walker, node->left_child);

    if (walker->failed) {
        return region_none();
    }
    return visit_declaration(walker, index, index * 2, node->name_index,
                             first);
}

static Region visit_assignment(Walker *walker, size_t index,
                               const TypedNode *node)
{
    const Binding *binding = lookup(walker, node->name_index, false);
    Region first = visit(walker, node->left_child);

    (void)index;
    if (!walker->failed && binding != NULL) {
        assign_var(walker, binding->index, first, node,
                   symbol_name(walker->program, node->name_index));
    }
    return region_none();
}

static Region visit_member_write(Walker *walker, size_t index,
                                 const TypedNode *node)
{
    const TypedProgram *program = walker->program;
    const TypedNode *target = typed_program_node(program, node->left_child);
    Region second = visit(walker, node->right_child);
    Region first;
    size_t i;

    (void)index;
    if (!visit_inner_region_none13(walker, &target, program)) {
        return region_none();
    }
    if (target != NULL && target->kind == TYPED_IDENTIFIER &&
        !target->is_self &&
        lookup(walker, target->name_index, false) != NULL) {
        /* A struct held by value: the write changes the variable, at the
           member path when every step of it is a known field. */
        size_t reversed[MAX_SHAPE];
        size_t path[MAX_SHAPE];
        size_t length = {0};

        visit_inner_length(program, node, &reversed, &i, &path, &length);
        assign_var_at(walker, lookup(walker, target->name_index, false)->index,
                      second, node, symbol_name(program, target->name_index),
                      path, length);
        return region_none();
    }
    first = visit(walker, node->left_child);
    check_write(walker,
                target != NULL && target->is_self
                    ? region_strip(walker->receiver)
                    : region_at(0),
                second, node, destination_name(program, node->left_child));
    (void)first;
    return region_none();
}

static Region visit_write(Walker *walker, size_t index, const TypedNode *node)
{
    Region first, second;

    (void)index;
    visit_inner_visit16(&first, walker, node, &second, walker->program);
    return region_none();
}

static Region visit_element_write(Walker *walker, size_t index,
                                  const TypedNode *node)
{
    Region first, second;
    size_t i;

    (void)index;
    visit_inner_owner(walker, node, &first, &i, walker->program, &second);
    return region_none();
}

static Region visit_store_alloc(Walker *walker, size_t index,
                                const TypedNode *node)
{
    Region store = store_region(walker, node);
    Region value = visit(walker, node->left_child);

    (void)index;
    if (!walker->failed) {
        check_write(walker, store, value, node,
                    symbol_name(walker->program, node->name_index));
    }
    return store;
}

/* A value made in a store lives in the store the node names. */
static Region visit_creation(Walker *walker, size_t index,
                             const TypedNode *node)
{
    (void)index;
    visit_children(walker, node);
    return store_region(walker, node);
}

static Region visit_read(Walker *walker, size_t index, const TypedNode *node)
{
    Region first;
    size_t i;

    (void)index;
    if (!visit_inner_visit(&first, walker, node, &i, walker->program)) {
        return region_strip(first);
    }
    return contents_of(first);
}

static Region visit_return(Walker *walker, size_t index, const TypedNode *node)
{
    Region first;

    (void)index;
    if (node->left_child != TYPED_NO_NODE) {
        first = visit(walker, node->left_child);
        visit_inner_add_mask(walker, &first, walker->program, node);
    }
    return region_none();
}

static Region visit_for(Walker *walker, size_t index, const TypedNode *node)
{
    size_t mark;

    if (!visit_inner_iterable(walker, node, &mark, index, walker->program)) {
        return region_none();
    }
    walker->bindings.length = mark;
    return region_none();
}

static Region visit_match_temporary(Walker *walker, size_t index,
                                    const TypedNode *node)
{
    (void)index;
    return walker->temps[(size_t)node->integer_value_u];
}

static Region visit_match_bind(Walker *walker, size_t index,
                               const TypedNode *node)
{
    size_t temp = (size_t)node->integer_value_u;
    Region first = visit(walker, node->left_child);

    (void)index;
    visit_inner_joined(walker, &temp, &first);
    return visit(walker, node->right_child);
}

/* A struct built from its fields keeps them apart while it stays inline. */
static bool is_shaped_constructor(const TypedProgram *program,
                                  const TypedNode *node)
{
    return node->children_count != 0 && node->children_count <= MAX_SHAPE &&
           struct_field_count(program, node->type_index) ==
               node->children_count;
}

static Region visit_constructor(Walker *walker, size_t index,
                                const TypedNode *node)
{
    Region kids[MAX_SHAPE];
    size_t i;

    (void)index;
    if (!is_shaped_constructor(walker->program, node)) {
        return region_strip(visit_children(walker, node));
    }
    for (i = 0; i < node->children_count && !walker->failed; i++) {
        kids[i] = visit(walker, *(const size_t *)list_get(
                                    &walker->program->children,
                                    node->children_start + i));
    }
    if (walker->failed) {
        return region_none();
    }
    return make_shape(&walker->shapes, kids, node->children_count);
}

static Region visit_member_access(Walker *walker, size_t index,
                                  const TypedNode *node)
{
    Region first;
    size_t field = {0};

    (void)index;
    if (!visit_inner_receiver(walker->program, node, &first, walker, &field)) {
        return region_strip(first);
    }
    return shape_kid(&walker->shapes, first, field);
}

static Region visit_sum_inject(Walker *walker, size_t index,
                               const TypedNode *node)
{
    const TypedType *type = typed_program_type(walker->program, node->type_index);
    Region kids[MAX_SHAPE];
    Region first;
    size_t i;

    (void)index;
    if (!visit_inner_region_none(&first, node, walker, type, &i, &kids)) {
        return region_strip(first);
    }
    kids[node->integer_value] = first;
    return make_shape(&walker->shapes, kids, type->child_count);
}

static Region visit_sum_payload(Walker *walker, size_t index,
                                const TypedNode *node)
{
    Region first;

    (void)index;
    if (!visit_inner_source(walker->program, node, &first, walker)) {
        return region_strip(first);
    }
    return shape_kid(&walker->shapes, first, (size_t)node->integer_value);
}

static Region visit_call_node(Walker *walker, size_t index,
                              const TypedNode *node)
{
    (void)node;
    return visit_call(walker, index);
}

static Region visit_nothing(Walker *walker, size_t index, const TypedNode *node)
{
    (void)walker;
    (void)index;
    (void)node;
    return region_none();
}

static const NodeVisitor node_visitors[] = {
    [TYPED_BLOCK] = visit_block,
    [TYPED_SCOPE] = visit_scope,
    [TYPED_PARALLEL] = visit_parallel,
    [TYPED_PARALLEL_ARM] = visit_parallel_arm,
    [TYPED_PARALLEL_RESULT] = visit_parallel_result,
    [TYPED_IDENTIFIER] = visit_identifier,
    [TYPED_LOCAL_DECLARATION] = visit_declaration_node,
    [TYPED_DECLARATION] = visit_declaration_node,
    [TYPED_ASSIGNMENT] = visit_assignment,
    [TYPED_MEMBER_WRITE] = visit_member_write,
    [TYPED_HANDLE_WRITE] = visit_write,
    [TYPED_LIST_PUSH] = visit_write,
    [TYPED_BUILDER_APPEND] = visit_write,
    [TYPED_BUILDER_APPEND_BYTE] = visit_write,
    [TYPED_LIST_SET] = visit_element_write,
    [TYPED_RECORD_SET] = visit_element_write,
    [TYPED_TABLE_SET] = visit_element_write,
    [TYPED_STORE_ALLOC] = visit_store_alloc,
    [TYPED_STRING_COPY] = visit_creation,
    [TYPED_STRING_CONCAT] = visit_creation,
    [TYPED_STRING_FROM_FIXED] = visit_creation,
    [TYPED_BUILDER_CREATE] = visit_creation,
    [TYPED_LIST_CREATE] = visit_creation,
    [TYPED_TABLE_CREATE] = visit_creation,
    [TYPED_CHANNEL_CREATE] = visit_creation,
    [TYPED_HANDLE_READ] = visit_read,
    [TYPED_LIST_GET] = visit_read,
    [TYPED_LIST_GET_OPTIONAL] = visit_read,
    [TYPED_LIST_POP] = visit_read,
    [TYPED_TABLE_GET] = visit_read,
    [TYPED_TABLE_GET_OPTIONAL] = visit_read,
    [TYPED_RECORD_GET] = visit_read,
    [TYPED_SLICE_GET] = visit_read,
    [TYPED_RETURN] = visit_return,
    [TYPED_FOR] = visit_for,
    [TYPED_MATCH_TEMPORARY] = visit_match_temporary,
    [TYPED_MATCH_BIND_EXPRESSION] = visit_match_bind,
    [TYPED_MATCH_BIND_STATEMENT] = visit_match_bind,
    [TYPED_STRUCT_CONSTRUCTOR] = visit_constructor,
    [TYPED_MEMBER_ACCESS] = visit_member_access,
    [TYPED_SUM_INJECT] = visit_sum_inject,
    [TYPED_SUM_PAYLOAD] = visit_sum_payload,
    [TYPED_FUNCTION_CALL] = visit_call_node,
    [TYPED_FUNCTION_DECLARATION] = visit_nothing,
};

static Region visit_inner(Walker *walker, size_t index)
{
    const TypedNode *node = typed_program_node(walker->program, index);

    if ((size_t)node->kind < NYX_COUNT_OF(node_visitors) &&
        node_visitors[node->kind] != NULL) {
        return node_visitors[node->kind](walker, index, node);
    }
    return visit_children(walker, node);
}

static Region visit(Walker *walker, size_t index)
{
    const TypedNode *node;
    Region result;

    if (walker->failed) {
        return region_none();
    }
    result = visit_inner(walker, index);
    node = typed_program_node(walker->program, index);
    if (node == NULL || !type_holds_pointers(walker->program, node->type_index)) {
        return region_none();
    }
    if (result.shape_count != 0) {
        /* Shapes mean fields or alternatives, so they are only valid on the
           type that has them. */
        const TypedType *type = typed_program_type(walker->program,
                                                   node->type_index);

        if (type == NULL ||
            (type->kind != NYX_TYPE_STRUCT && type->kind != NYX_TYPE_UNION)) {
            return region_strip(result);
        }
    }
    return result;
}

/* Rebuilds the shape arena from the regions still in use, so a long fixed-point
   run does not keep every intermediate value. */
static bool compact_shapes(Walker *walker)
{
    List fresh;
    bool ok = true;
    size_t i;

    list_init(&fresh, sizeof(Region));
    for (i = 0; i < walker->vars.length; i++) {
        Var *var = (Var *)list_get(&walker->vars, i);

        var->value = region_clone(&walker->shapes, var->value, &fresh, &ok);
    }
    for (i = 0; i < walker->used_temps.length; i++) {
        size_t temp = *(const size_t *)list_get(&walker->used_temps, i);

        walker->temps[temp] = region_clone(&walker->shapes, walker->temps[temp], &fresh, &ok);
    }
    list_free(&walker->shapes.cells);
    walker->shapes.cells = fresh;
    if (!ok || walker->shapes.failed) {
        fail_oom(walker);
        return false;
    }
    return true;
}

/* A parameter is a variable of the frame, or the frame's Store. */
static bool bind_parameter(Walker *walker, const TypedNode *function, size_t i)
{
    const TypedProgram *program = walker->program;
    size_t child = *(const size_t *)list_get(&program->children,
                                             function->children_start + i);
    const TypedNode *parameter = typed_program_node(program, child);
    size_t slot = find_var(walker, child * 2, 1);
    Var *var;

    if (slot == TYPED_NO_NODE) {
        return false;
    }
    if (typed_program_node_kind(program, child) == NYX_TYPE_STORE) {
        bind(walker, parameter->name_index, true, 1,
             slot_mask(walker->self, i));
        return true;
    }
    if (type_holds_pointers(program, parameter->type_index)) {
        var = (Var *)list_get(&walker->vars, slot);
        var->value = param_region(&walker->shapes, walker->self, i);
        if (i == 0) {
            walker->receiver = var->value;
        }
    }
    bind(walker, parameter->name_index, false, slot, 0);
    return true;
}

static bool visit_function_frame(Walker *walker, const TypedNode *function)
{
    size_t i;

    for (i = 0; i < function->children_count && !walker->failed; i++) {
        if (!bind_parameter(walker, function, i)) {
            return false;
        }
    }
    if (!walker->failed) {
        visit(walker, function->left_child);
    }
    return true;
}

/* Top-level statements run in the order they are written; declarations of
   functions, types and Stores are not statements. */
static void visit_top_level(Walker *walker)
{
    const TypedProgram *program = walker->program;
    size_t i;

    for (i = 0; i < program->roots.length && !walker->failed; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        TypedNodeKind kind = typed_program_node(program, root)->kind;

        if (kind != TYPED_FUNCTION_DECLARATION &&
            kind != TYPED_TYPE_DECLARATION && kind != TYPED_STORE_DECLARATION) {
            visit(walker, root);
        }
    }
}

static bool analyse_frame_step(Walker *walker, const TypedNode *function)
{
    walker->changed = false;
    walker->bindings.length = 0;
    walker->depth = walker->base;
    walker->receiver = region_none();
    if (function == NULL) {
        visit_top_level(walker);
        return true;
    }
    return visit_function_frame(walker, function);
}

/* Walks one frame until its variables stop changing. */
static bool analyse_frame(Walker *walker, size_t function_root)
{
    const TypedProgram *program = walker->program;
    const TypedNode *function = function_root == TYPED_NO_NODE
                                    ? NULL
                                    : typed_program_node(program, function_root);
    size_t i;

    walker->vars.length = 0;
    for (i = 0; i < walker->used_temps.length; i++) {
        walker->temps[*(const size_t *)list_get(&walker->used_temps, i)] =
            region_none();
    }
    walker->used_temps.length = 0;
    do {
        if (!analyse_frame_step(walker, function)) {
        return false;
    }
        if (!compact_shapes(walker)) {
            return false;
        }
    } while (walker->changed && !walker->failed);
    return !walker->failed;
}

static size_t count_functions(const TypedProgram *program)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        count += typed_program_node(
                     program, *(const size_t *)list_get(&program->roots, i))
                     ->kind == TYPED_FUNCTION_DECLARATION;
    }
    return count;
}

/* One summary per function, laid out and named in declaration order. */
static void layout_summaries(Walker *walker)
{
    const TypedProgram *program = walker->program;
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));

        if (node->kind == TYPED_FUNCTION_DECLARATION) {
            layout_summary(program, node,
                           &walker->summaries[walker->summary_count]);
            walker->summaries[walker->summary_count++].name = node->name_index;
        }
    }
}

static bool walker_open(Walker *walker, const TypedProgram *program,
                        ErrorState *errors)
{
    size_t temps = program->match_temporaries.length + 1;
    size_t functions = count_functions(program) + 1;
    size_t i;

    memset(walker, 0, sizeof(*walker));
    walker->program = program;
    walker->errors = errors;
    list_init(&walker->vars, sizeof(Var));
    list_init(&walker->bindings, sizeof(Binding));
    list_init(&walker->used_temps, sizeof(size_t));
    list_init(&walker->arm_results, sizeof(ArmResult));
    list_init(&walker->shapes.cells, sizeof(Region));
    walker->temps = nyx_malloc(temps * sizeof(*walker->temps));
    walker->summaries = nyx_malloc(functions * sizeof(*walker->summaries));
    if (walker->temps == NULL || walker->summaries == NULL) {
        error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        return false;
    }
    memset(walker->summaries, 0, functions * sizeof(*walker->summaries));
    walker->temp_count = temps;
    for (i = 0; i < temps; i++) {
        walker->temps[i] = region_none();
    }
    layout_summaries(walker);
    return true;
}

static void walker_close(Walker *walker)
{
    list_free(&walker->vars);
    list_free(&walker->bindings);
    list_free(&walker->used_temps);
    list_free(&walker->arm_results);
    list_free(&walker->shapes.cells);
    free(walker->temps);
    free(walker->summaries);
}

/* One pass over every function, then the top-level code. */
static bool analyse_program_once(Walker *walker)
{
    const TypedProgram *program = walker->program;
    size_t function_index = 0;
    size_t i;

    walker->flags_changed = false;
    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);

        if (typed_program_node(program, root)->kind !=
            TYPED_FUNCTION_DECLARATION) {
            continue;
        }
        walker->self = &walker->summaries[function_index++];
        walker->base = 1;
        if (!analyse_frame(walker, root)) {
            return false;
        }
    }
    walker->self = NULL;
    walker->base = 0;
    return analyse_frame(walker, TYPED_NO_NODE);
}

bool analyser_check_regions(const TypedProgram *program,
                            const Analysis *analysis, ErrorState *errors)
{
    Walker walker;
    bool ok;
    bool again;

    if (!analysis_has(analysis, ANALYSIS_SCOPE)) {
        return true;
    }
    ok = walker_open(&walker, program, errors);
    again = ok;
    /* Summaries only grow, so the loop ends; an error found before they
       settle is still an error once they have. */
    while (again) {
        ok = analyse_program_once(&walker);
        again = ok && walker.flags_changed;
    }
    walker_close(&walker);
    return ok;
}
