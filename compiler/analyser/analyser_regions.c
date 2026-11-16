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

typedef struct {
    const TypedProgram *program;
    ErrorState *errors;
    List vars;
    List bindings;
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
} Walker;

/* Shapes live in one append-only arena per analysis. Regions are small values
   passed by copy; a shape is referenced by index and never changed once
   written. Running out of memory only drops precision, but is also recorded so
   the analysis fails closed. */
static _Thread_local List shape_arena;
static _Thread_local bool shape_failed;

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

static Region shape_kid(Region region, size_t index)
{
    if (index < region.shape_count) {
        return *(const Region *)list_get(&shape_arena, region.shape + index);
    }
    return region_strip(region);
}

static Region region_join(Region left, Region right)
{
    Region result;

    if (left.lo == REGION_NONE) {
        return right;
    }
    if (right.lo == REGION_NONE) {
        return left;
    }
    result = region_strip(left);
    result.hi = left.hi > right.hi ? left.hi : right.hi;
    result.lo = left.lo < right.lo ? left.lo : right.lo;
    result.params = left.params | right.params;
    if (right.hi > left.hi) {
        result.origin = right.origin;
        result.origin_pos = right.origin_pos;
    }
    if (left.shape_count != 0 && left.shape_count == right.shape_count) {
        Region kids[MAX_SHAPE];
        size_t i;

        for (i = 0; i < left.shape_count; i++) {
            kids[i] = region_join(shape_kid(left, i), shape_kid(right, i));
        }
        result.shape = shape_arena.length;
        for (i = 0; i < left.shape_count; i++) {
            if (!list_push(&shape_arena, &kids[i])) {
                shape_failed = true;
                return region_strip(result);
            }
        }
        result.shape_count = left.shape_count;
    }
    return result;
}

static bool region_equal(Region left, Region right)
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
        if (!region_equal(shape_kid(left, i), shape_kid(right, i))) {
            return false;
        }
    }
    return true;
}

/* A value built from separately tracked members. The whole is their join. */
static Region make_shape(const Region *kids, size_t count)
{
    Region whole = region_none();
    size_t i;

    for (i = 0; i < count; i++) {
        whole = region_join(whole, region_strip(kids[i]));
    }
    if (whole.lo == REGION_NONE || count == 0 || count > MAX_SHAPE) {
        return whole;
    }
    whole.shape = shape_arena.length;
    for (i = 0; i < count; i++) {
        if (!list_push(&shape_arena, &kids[i])) {
            shape_failed = true;
            return region_strip(whole);
        }
    }
    whole.shape_count = count;
    return whole;
}

/* `base` with `value` written at the member path `path`. A base that does not
   track that path takes the value as a whole. */
static Region region_patch(Region base, const size_t *path, size_t length,
                           Region value)
{
    Region kids[MAX_SHAPE];
    size_t i;

    if (length == 0) {
        return region_join(base, value);
    }
    if (base.lo == REGION_NONE) {
        return region_strip(value);
    }
    if (base.shape_count == 0 || path[0] >= base.shape_count) {
        return region_join(base, region_strip(value));
    }
    for (i = 0; i < base.shape_count; i++) {
        kids[i] = shape_kid(base, i);
    }
    kids[path[0]] = region_patch(kids[path[0]], path + 1, length - 1, value);
    return make_shape(kids, base.shape_count);
}

/* Copies a region and the shapes under it into `to`, so the arena can be
   rebuilt from the regions still in use. */
static Region region_clone(Region region, List *to, bool *ok)
{
    Region kids[MAX_SHAPE];
    size_t i;

    if (region.shape_count == 0) {
        return region;
    }
    for (i = 0; i < region.shape_count; i++) {
        kids[i] = region_clone(shape_kid(region, i), to, ok);
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
    static const NippyType pointer_free[] = {
        NIPPY_TYPE_UNKNOWN, NIPPY_TYPE_INT32, NIPPY_TYPE_BOOL,
        NIPPY_TYPE_BYTE, NIPPY_TYPE_NIL, NIPPY_TYPE_FUNCTION,
        NIPPY_TYPE_STRING, NIPPY_TYPE_RANGE, NIPPY_TYPE_STRING_LITERAL,
        NIPPY_TYPE_ERROR, NIPPY_TYPE_INT8, NIPPY_TYPE_INT16,
        NIPPY_TYPE_INT64, NIPPY_TYPE_UINT16, NIPPY_TYPE_UINT32,
        NIPPY_TYPE_UINT64, NIPPY_TYPE_FLOAT32, NIPPY_TYPE_FLOAT64,
    };
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL) {
        return false;
    }
    for (i = 0; i < NIPPY_COUNT_OF(pointer_free); i++) {
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

    if (type == NULL || type->kind != NIPPY_TYPE_STRUCT ||
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

    if (type == NULL || type->kind != NIPPY_TYPE_STRUCT ||
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

    return strncmp(name, "nyx_", 4) == 0 ? "method or generic function" : name;
}

/* "'name'" for a known Store, otherwise a generic phrase. */
static void describe_origin(const TypedProgram *program, size_t origin,
                            char *text, size_t capacity)
{
    if (origin == TYPED_NO_NODE) {
        snprintf(text, capacity, "an inner scope");
    } else {
        snprintf(text, capacity, "Store '%s'", symbol_name(program, origin));
    }
}

/* The variable or Store a write lands in, for diagnostics. */
static const char *destination_name(const TypedProgram *program, size_t index)
{
    const TypedNode *node = typed_program_node(program, index);

    while (node != NULL &&
           (node->kind == TYPED_MEMBER_ACCESS || node->kind == TYPED_HANDLE_READ ||
            node->kind == TYPED_LIST_GET || node->kind == TYPED_SLICE_GET ||
            node->kind == TYPED_RECORD_GET || node->kind == TYPED_TABLE_GET)) {
        node = typed_program_node(program, node->left_child);
    }
    if (node != NULL && node->kind == TYPED_IDENTIFIER && !node->is_self) {
        return symbol_name(program, node->name_index);
    }
    return "longer-lived memory";
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
        char origin[NIPPY_IDENTIFIER_MAX + 16];

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
    joined = region_patch(var->value, path, path_length, value);
    if (!region_equal(joined, var->value)) {
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
    char origin[NIPPY_IDENTIFIER_MAX + 16];

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

/* A fixed-length list or a record lives inside its variable, so a write into
   it changes the variable. Returns the variable's binding, or NULL when the
   receiver is not such a local. */
static const Binding *by_value_receiver(const Walker *walker, size_t index)
{
    const TypedNode *node = typed_program_node(walker->program, index);
    const TypedType *type;

    if (node == NULL) {
        return NULL;
    }
    type = typed_program_type(walker->program, node->type_index);
    if (type == NULL ||
        (type->kind != NIPPY_TYPE_FIXED_LIST && type->kind != NIPPY_TYPE_RECORD)) {
        return NULL;
    }
    while (node != NULL && node->kind == TYPED_MEMBER_ACCESS) {
        node = typed_program_node(walker->program, node->left_child);
    }
    if (node == NULL || node->kind != TYPED_IDENTIFIER || node->is_self) {
        return NULL;
    }
    return lookup(walker, node->name_index, false);
}

static Region visit_children(Walker *walker, const TypedNode *node)
{
    Region result = region_none();
    size_t i;

    if (node->left_child != TYPED_NO_NODE) {
        result = region_join(result, visit(walker, node->left_child));
    }
    if (node->right_child != TYPED_NO_NODE) {
        result = region_join(result, visit(walker, node->right_child));
    }
    for (i = 0; i < node->children_count && !walker->failed; i++) {
        size_t child = *(const size_t *)list_get(&walker->program->children,
                                                 node->children_start + i);

        result = region_join(result, visit(walker, child));
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
    char origin[NIPPY_IDENTIFIER_MAX + 16];
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
    for (i = 0; i < function->children_count && i < MAX_TRACKED_PARAMS - 1; i++) {
        const TypedNode *parameter = typed_program_node(
            program, *(const size_t *)list_get(&program->children,
                                               function->children_start + i));
        size_t fields = parameter == NULL
                            ? 0
                            : struct_field_count(program, parameter->type_index);
        size_t count = fields >= 2 && fields <= MAX_PARAM_FIELDS ? fields : 1;
        size_t f;

        if (next + count > MAX_TRACKED_PARAMS - 1) {
            count = 1;
        }
        if (next + count > MAX_TRACKED_PARAMS - 1) {
            break;
        }
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
static Region param_region(const Summary *summary, size_t param)
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
    return make_shape(kids, count);
}

/* The part of an argument that parameter slot `field` stands for. */
static Region argument_part(Region argument, size_t field, size_t slots)
{
    if (slots >= 2 && argument.shape_count == slots) {
        return shape_kid(argument, field);
    }
    return region_strip(argument);
}

static void check_call_contract_source(size_t *i, size_t count, const Summary *summary, Walker *walker, const Region *arguments, size_t *j, const TypedNode *node)
{
    for ((*i) = 0; (*i) < count && (*i) < summary->param_count && !walker->failed; (*i)++) {
        size_t source_index = param_index((*i));
        size_t source_slots = summary->param_slots[source_index];
        size_t f;

        for (f = 0; f < source_slots && !walker->failed; f++) {
            size_t slot = summary->param_base[source_index] + f;
            Region source = argument_part(arguments[(*i)], f, source_slots);
            uint64_t flow = summary->flow[slot];

            for ((*j) = 0; (*j) < count && (*j) < summary->param_count && !walker->failed;
                 (*j)++) {
                size_t sink_index = param_index((*j));
                size_t sink_slots = summary->param_slots[sink_index];
                size_t g;

                for (g = 0; g < sink_slots && !walker->failed; g++) {
                    size_t target = summary->param_base[sink_index] + g;

                    if ((flow >> target) & 1) {
                        check_flow(walker, node, source,
                                   argument_part(arguments[(*j)], g, sink_slots),
                                   summary, slot);
                    }
                }
            }
            if (!walker->failed && ((summary->to_global >> slot) & 1)) {
                check_global_flow(walker, node, source, summary);
            }
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
    size_t i, j;

    if (summary == NULL) {
        /* Every argument may go into every other; a direct call to a function
           with no summary may also reach global memory. */
        for (i = 0; i < count && !walker->failed; i++) {
            for (j = 0; j < count && !walker->failed; j++) {
                check_flow(walker, node, region_strip(arguments[i]),
                           region_strip(arguments[j]), NULL, 0);
            }
            if (!indirect && !walker->failed) {
                check_global_flow(walker, node, region_strip(arguments[i]),
                                  NULL);
            }
        }
        return;
    }
    check_call_contract_source(&i, count, summary, walker, arguments, &j, node);
}

static Region visit_call(Walker *walker, size_t index)
{
    const TypedNode *node = typed_program_node(walker->program, index);
    Region *arguments = NULL;
    Region result = region_none();
    const Summary *summary;
    bool indirect;
    size_t count = node->children_count;
    size_t i;

    if (count != 0) {
        arguments = nippy_malloc(count * sizeof(*arguments));
        if (arguments == NULL) {
            fail_oom(walker);
            return region_none();
        }
    }
    for (i = 0; i < count && !walker->failed; i++) {
        size_t child = *(const size_t *)list_get(&walker->program->children,
                                                 node->children_start + i);

        arguments[i] = visit(walker, child);
    }
    if (!walker->failed) {
        indirect = lookup(walker, node->name_index, false) != NULL;
        summary = indirect ? NULL : find_summary(walker, node->name_index);
        check_call_contract(walker, node, summary, indirect, arguments, count);
        for (i = 0; i < count; i++) {
            size_t slots;
            size_t f;

            if (indirect || summary == NULL || i >= summary->param_count) {
                result = region_join(result, region_strip(arguments[i]));
                continue;
            }
            slots = summary->param_slots[param_index(i)];
            for (f = 0; f < slots; f++) {
                if ((summary->returned >>
                     (summary->param_base[param_index(i)] + f)) & 1) {
                    result = region_join(
                        result, argument_part(arguments[i], f, slots));
                }
            }
        }
        /* The result is reachable from the arguments the callee derives it from,
           and from global memory only if the callee may return such a value. */
        if (indirect || summary == NULL || summary->returns_global) {
            result = region_join(result, region_at(0));
        }
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

static bool visit_inner_type(const TypedProgram *program, const TypedNode *node, Walker *walker, const Binding **binding, Region *outcome)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    

    if (type != NULL && type->kind == NIPPY_TYPE_STORE) {
        do { *outcome = (store_region(walker, node)); return true; } while (0);
    }
    if (type != NULL && type->kind == NIPPY_TYPE_FUNCTION &&
        lookup(walker, node->name_index, false) == NULL) {
        const Summary *callee = find_summary(walker, node->name_index);

        if (callee != NULL && callee->to_global != 0) {
            error_report_at(walker->errors, node->pos,
                            MESSAGE_ANALYSER_ESCAPE_FUNCTION_VALUE,
                            callee_name(program, node->name_index));
            walker->failed = true;
        }
        do { *outcome = (region_none()); return true; } while (0);
    }
    if (node->is_self) {
        do { *outcome = (walker->receiver); return true; } while (0);
    }
    (*binding) = lookup(walker, node->name_index, false);
    if ((*binding) == NULL) {
        do { *outcome = (region_at(0)); return true; } while (0);
    }
    return false;
}

static bool visit_inner_region_none(Region *first, const TypedNode *node, Walker *walker, const TypedType *type, size_t *i, Region (*kids)[32])
{
    (*first) = node->left_child == TYPED_NO_NODE
                ? region_none()
                : visit(walker, node->left_child);
    if (type == NULL || type->kind != NIPPY_TYPE_UNION ||
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
        char origin[NIPPY_IDENTIFIER_MAX + 16];

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
            (container_type->kind == NIPPY_TYPE_FIXED_LIST ||
             container_type->kind == NIPPY_TYPE_RECORD)) {
            return false;
        }
    }
    return true;
}

static bool visit_inner_kids(const TypedNode *node, const TypedProgram *program, Walker *walker, size_t *i, Region *outcome2)
{
    Region kids[MAX_SHAPE];

    if (node->children_count == 0 || node->children_count > MAX_SHAPE ||
        struct_field_count(program, node->type_index) !=
            node->children_count) {
        do { *outcome2 = (region_strip(visit_children(walker, node))); return true; } while (0);
    }
    for ((*i) = 0; (*i) < node->children_count && !walker->failed; (*i)++) {
        kids[(*i)] = visit(walker, *(const size_t *)list_get(
                                    &program->children,
                                    node->children_start + (*i)));
    }
    if (walker->failed) {
        do { *outcome2 = (region_none()); return true; } while (0);
    }
    do { *outcome2 = (make_shape(kids, node->children_count)); return true; } while (0);
    return false;
}

static bool visit_inner_source(const TypedProgram *program, const TypedNode *node, Region *first, Walker *walker)
{
    const TypedNode *source = typed_program_node(program, node->left_child);
    const TypedType *type = source == NULL ? NULL
                            : typed_program_type(program, source->type_index);

    (*first) = visit(walker, node->left_child);
    if (type == NULL || type->kind != NIPPY_TYPE_UNION ||
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
        Region joined = region_join(walker->temps[(*temp)], (*first));

        if (!region_equal(joined, walker->temps[(*temp)])) {
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

static Region visit_inner(Walker *walker, size_t index)
{
    const TypedNode *node = typed_program_node(walker->program, index);
    const TypedProgram *program = walker->program;
    Region first, second;
    size_t mark;
    size_t i;

    switch (node->kind) {
    case TYPED_BLOCK: {
        mark = walker->bindings.length;
        visit_children(walker, node);
        walker->bindings.length = mark;
        return region_none();
    }
    case TYPED_SCOPE: {
        size_t depth = walker->depth + 1;

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
    case TYPED_IDENTIFIER: {
        const Binding *binding = {0};
        {
        Region outcome;

        if (visit_inner_type(program, node, walker, &binding, &outcome)) {
            return outcome;
        }
    }
        return ((const Var *)list_get(&walker->vars, binding->index))->value;
    }
    case TYPED_LOCAL_DECLARATION:
    case TYPED_DECLARATION: {
        first = node->left_child == TYPED_NO_NODE
                    ? region_none()
                    : visit(walker, node->left_child);
        if (walker->failed) {
            return region_none();
        }
        return visit_declaration(walker, index, index * 2, node->name_index,
                                 first);
    }
    case TYPED_ASSIGNMENT: {
        const Binding *binding = lookup(walker, node->name_index, false);

        first = visit(walker, node->left_child);
        if (!walker->failed && binding != NULL) {
            assign_var(walker, binding->index, first, node,
                       symbol_name(program, node->name_index));
        }
        return region_none();
    }
    case TYPED_MEMBER_WRITE: {
        const TypedNode *target = typed_program_node(program, node->left_child);

        second = visit(walker, node->right_child);
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
            assign_var_at(walker,
                          lookup(walker, target->name_index, false)->index,
                          second, node,
                          symbol_name(program, target->name_index), path,
                          length);
        } else {
            Region container = region_at(0);

            first = visit(walker, node->left_child);
            if (target != NULL && target->is_self) {
                container = region_strip(walker->receiver);
            }
            check_write(walker, container, second, node,
                        destination_name(program, node->left_child));
            (void)first;
        }
        return region_none();
    }
    case TYPED_HANDLE_WRITE:
    case TYPED_LIST_PUSH:
    case TYPED_BUILDER_APPEND:
    case TYPED_BUILDER_APPEND_BYTE: {
        visit_inner_visit16(&first, walker, node, &second, program);
        return region_none();
    }
    case TYPED_LIST_SET:
    case TYPED_RECORD_SET:
    case TYPED_TABLE_SET: {
        visit_inner_owner(walker, node, &first, &i, program, &second);
        return region_none();
    }
    case TYPED_STORE_ALLOC: {
        Region store = store_region(walker, node);

        second = visit(walker, node->left_child);
        if (!walker->failed) {
            check_write(walker, store, second, node,
                        symbol_name(program, node->name_index));
        }
        return store;
    }
    case TYPED_STRING_COPY: case TYPED_STRING_CONCAT:
    case TYPED_STRING_FROM_FIXED: case TYPED_BUILDER_CREATE:
    case TYPED_LIST_CREATE: case TYPED_TABLE_CREATE: {
        visit_children(walker, node);
        return store_region(walker, node);
    }
    case TYPED_HANDLE_READ: case TYPED_LIST_GET: case TYPED_LIST_GET_OPTIONAL:
    case TYPED_LIST_POP: case TYPED_TABLE_GET: case TYPED_TABLE_GET_OPTIONAL:
    case TYPED_RECORD_GET: case TYPED_SLICE_GET: {
        if (!visit_inner_visit(&first, walker, node, &i, program)) {
        return region_strip(first);
    }
        return contents_of(first);
    }
    case TYPED_RETURN: {
        if (node->left_child != TYPED_NO_NODE) {
            first = visit(walker, node->left_child);
            visit_inner_add_mask(walker, &first, program, node);
        }
        return region_none();
    }
    case TYPED_FOR: {
        if (!visit_inner_iterable(walker, node, &mark, index, program)) {
        return region_none();
    }
        walker->bindings.length = mark;
        return region_none();
    }
    case TYPED_MATCH_TEMPORARY:
        return walker->temps[(size_t)node->integer_value_u];
    case TYPED_MATCH_BIND_EXPRESSION:
    case TYPED_MATCH_BIND_STATEMENT: {
        size_t temp = (size_t)node->integer_value_u;

        first = visit(walker, node->left_child);
        visit_inner_joined(walker, &temp, &first);
        return visit(walker, node->right_child);
    }
    case TYPED_STRUCT_CONSTRUCTOR: {
        {
        Region outcome2;

        if (visit_inner_kids(node, program, walker, &i, &outcome2)) {
            return outcome2;
        }
    }
    }
        return region_none();
    case TYPED_MEMBER_ACCESS: {
        size_t field = {0};
        if (!visit_inner_receiver(program, node, &first, walker, &field)) {
        return region_strip(first);
    }
        return shape_kid(first, field);
    }
    case TYPED_SUM_INJECT: {
        const TypedType *type = typed_program_type(program, node->type_index);
        Region kids[MAX_SHAPE];

        if (!visit_inner_region_none(&first, node, walker, type, &i, &kids)) {
        return region_strip(first);
    }
        kids[node->integer_value] = first;
        return make_shape(kids, type->child_count);
    }
    case TYPED_SUM_PAYLOAD: {
        if (!visit_inner_source(program, node, &first, walker)) {
        return region_strip(first);
    }
        return shape_kid(first, (size_t)node->integer_value);
    }
    case TYPED_FUNCTION_CALL:
        return visit_call(walker, index);
    case TYPED_FUNCTION_DECLARATION:
        return region_none();
    default:
        return visit_children(walker, node);
    }
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
            (type->kind != NIPPY_TYPE_STRUCT && type->kind != NIPPY_TYPE_UNION)) {
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

        var->value = region_clone(var->value, &fresh, &ok);
    }
    for (i = 0; i < walker->used_temps.length; i++) {
        size_t temp = *(const size_t *)list_get(&walker->used_temps, i);

        walker->temps[temp] = region_clone(walker->temps[temp], &fresh, &ok);
    }
    list_free(&shape_arena);
    shape_arena = fresh;
    if (!ok || shape_failed) {
        fail_oom(walker);
        return false;
    }
    return true;
}

static bool analyse_frame_step(Walker *walker, const TypedNode *function, size_t *i, const TypedProgram *program)
{
    walker->changed = false;
    walker->bindings.length = 0;
    walker->depth = walker->base;
    walker->receiver = region_none();
    if (function != NULL) {
        for ((*i) = 0; (*i) < function->children_count && !walker->failed; (*i)++) {
            size_t child = *(const size_t *)list_get(
                &program->children, function->children_start + (*i));
            const TypedNode *parameter = typed_program_node(program, child);
            const TypedType *type = typed_program_type(
                program, parameter->type_index);
            size_t slot = find_var(walker, child * 2, 1);

            if (slot == TYPED_NO_NODE) {
                return false;
            }
            if (type != NULL && type->kind == NIPPY_TYPE_STORE) {
                bind(walker, parameter->name_index, true, 1,
                     slot_mask(walker->self, (*i)));
            } else {
                if (type_holds_pointers(program, parameter->type_index)) {
                    Var *var = (Var *)list_get(&walker->vars, slot);

                    var->value = param_region(walker->self, (*i));
                    if ((*i) == 0) {
                        walker->receiver = var->value;
                    }
                }
                bind(walker, parameter->name_index, false, slot, 0);
            }
        }
        if (!walker->failed) {
            visit(walker, function->left_child);
        }
    } else {
        for ((*i) = 0; (*i) < program->roots.length && !walker->failed; (*i)++) {
            size_t root = *(const size_t *)list_get(&program->roots, (*i));
            const TypedNode *node = typed_program_node(program, root);

            if (node->kind == TYPED_FUNCTION_DECLARATION ||
                node->kind == TYPED_TYPE_DECLARATION ||
                node->kind == TYPED_STORE_DECLARATION) {
                continue;
            }
            visit(walker, root);
        }
    }
    return true;
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
        if (!analyse_frame_step(walker, function, &i, program)) {
        return false;
    }
        if (!compact_shapes(walker)) {
            return false;
        }
    } while (walker->changed && !walker->failed);
    return !walker->failed;
}

bool analyser_check_regions(const TypedProgram *program, ErrorState *errors)
{
    Walker walker;
    size_t function_count = 0;
    size_t i;
    bool ok = false;

    if (!program->contains_scope) {
        return true;
    }
    memset(&walker, 0, sizeof(walker));
    walker.program = program;
    walker.errors = errors;
    list_init(&walker.vars, sizeof(Var));
    list_init(&walker.bindings, sizeof(Binding));
    list_init(&walker.used_temps, sizeof(size_t));
    list_init(&shape_arena, sizeof(Region));
    shape_failed = false;
    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);

        if (typed_program_node(program, root)->kind == TYPED_FUNCTION_DECLARATION) {
            function_count++;
        }
    }
    if (program->match_temporaries.length >= SIZE_MAX / sizeof(*walker.temps) ||
        function_count >= SIZE_MAX / sizeof(*walker.summaries)) {
        error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        goto done;
    }
    walker.temps = nippy_malloc((program->match_temporaries.length + 1) *
                                sizeof(*walker.temps));
    walker.summaries = nippy_malloc((function_count + 1) *
                                    sizeof(*walker.summaries));
    if (walker.temps != NULL) {
        memset(walker.temps, 0, (program->match_temporaries.length + 1) *
                                    sizeof(*walker.temps));
    }
    if (walker.summaries != NULL) {
        memset(walker.summaries, 0, (function_count + 1) *
                                        sizeof(*walker.summaries));
    }
    if (walker.temps == NULL || walker.summaries == NULL) {
        error_report(errors, MESSAGE_ANALYSER_OUT_OF_MEMORY);
        goto done;
    }
    walker.temp_count = program->match_temporaries.length + 1;
    for (i = 0; i < walker.temp_count; i++) {
        walker.temps[i] = region_none();
    }
    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node->kind == TYPED_FUNCTION_DECLARATION) {
            layout_summary(program, node,
                           &walker.summaries[walker.summary_count]);
            walker.summaries[walker.summary_count++].name = node->name_index;
        }
    }
    /* Summaries only grow, so the loop ends; an error found before they
       settle is still an error once they have. */
    do {
        size_t function_index = 0;

        walker.flags_changed = false;
        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node->kind != TYPED_FUNCTION_DECLARATION) {
                continue;
            }
            walker.self = &walker.summaries[function_index++];
            walker.base = 1;
            if (!analyse_frame(&walker, root)) {
                goto done;
            }
        }
        walker.self = NULL;
        walker.base = 0;
        if (!analyse_frame(&walker, TYPED_NO_NODE)) {
            goto done;
        }
    } while (walker.flags_changed);
    ok = true;
done:
    list_free(&walker.vars);
    list_free(&walker.bindings);
    list_free(&walker.used_temps);
    list_free(&shape_arena);
    free(walker.temps);
    free(walker.summaries);
    return ok;
}
