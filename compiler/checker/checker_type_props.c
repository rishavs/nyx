#include "checker_internal.h"

/* Properties of a type that follow from its parts: a value can be printed,
   sent to another thread, or hold an error when its parts can. Each property
   is a table saying what each kind of type contributes; one engine walks the
   parts, so a new kind is a new table row rather than a new switch. */

typedef enum {
    VERDICT_NO,    /* the property fails for this kind (also: kind not listed) */
    VERDICT_YES,   /* the property holds with no further inspection */
    VERDICT_PARTS  /* decided by the parts of the type */
} Verdict;

typedef struct {
    const unsigned char *verdicts; /* indexed by NyxType */
    size_t count;
    bool any; /* parts: one part is enough (true) or every part is needed */
} PropertyRules;

#define NUMERIC_KINDS(verdict)                                                \
    [NYX_TYPE_INT8] = verdict, [NYX_TYPE_INT16] = verdict,                    \
    [NYX_TYPE_INT32] = verdict, [NYX_TYPE_INT64] = verdict,                   \
    [NYX_TYPE_BYTE] = verdict, [NYX_TYPE_UINT16] = verdict,                   \
    [NYX_TYPE_UINT32] = verdict, [NYX_TYPE_UINT64] = verdict,                 \
    [NYX_TYPE_FLOAT32] = verdict, [NYX_TYPE_FLOAT64] = verdict

static Verdict verdict_of(const PropertyRules *rules, NyxType kind)
{
    return (size_t)kind < rules->count ? rules->verdicts[kind] : VERDICT_NO;
}

static bool type_has_property(const TypedProgram *program,
                              const PropertyRules *rules, size_t type_index,
                              unsigned depth)
{
    const TypedType *type = typed_program_type(program, type_index);
    Verdict verdict;
    size_t i;

    if (type == NULL || depth == 0) {
        return false;
    }
    verdict = verdict_of(rules, type->kind);
    if (verdict != VERDICT_PARTS) {
        return verdict == VERDICT_YES;
    }
    for (i = 0; i < typed_type_part_count(program, type); i++) {
        size_t part;
        bool holds = typed_type_part(program, type, i, &part) &&
                     type_has_property(program, rules, part, depth - 1);

        if (holds == rules->any) {
            return holds;
        }
    }
    return !rules->any;
}

#define RULES(table, is_any) {table, NYX_COUNT_OF(table), is_any}

static const unsigned char printable_verdicts[] = {
    NUMERIC_KINDS(VERDICT_YES),
    [NYX_TYPE_BOOL] = VERDICT_YES,
    [NYX_TYPE_NIL] = VERDICT_YES,
    [NYX_TYPE_STRING] = VERDICT_YES,
    [NYX_TYPE_DYNAMIC_STRING] = VERDICT_YES,
    [NYX_TYPE_TEXT_BUILDER] = VERDICT_YES,
    [NYX_TYPE_ERROR] = VERDICT_YES,
    [NYX_TYPE_FUNCTION] = VERDICT_YES,
    [NYX_TYPE_HANDLE] = VERDICT_YES,
    [NYX_TYPE_RECORD] = VERDICT_PARTS,
    [NYX_TYPE_TABLE] = VERDICT_PARTS,
    [NYX_TYPE_FIXED_LIST] = VERDICT_PARTS,
    [NYX_TYPE_LIST] = VERDICT_PARTS,
    [NYX_TYPE_SLICE] = VERDICT_PARTS,
    [NYX_TYPE_UNION] = VERDICT_PARTS,
    [NYX_TYPE_STRUCT] = VERDICT_PARTS,
};

/* A sendable value is inline: no Text, Handle, List, Slice, Table, TextBuilder,
   Store, function or channel. */
static const unsigned char sendable_verdicts[] = {
    NUMERIC_KINDS(VERDICT_YES),
    [NYX_TYPE_BOOL] = VERDICT_YES,
    [NYX_TYPE_NIL] = VERDICT_YES,
    [NYX_TYPE_ERROR] = VERDICT_YES,
    [NYX_TYPE_STRING] = VERDICT_YES,
    [NYX_TYPE_STRUCT] = VERDICT_PARTS,
    [NYX_TYPE_UNION] = VERDICT_PARTS,
    [NYX_TYPE_FIXED_LIST] = VERDICT_PARTS,
    [NYX_TYPE_RANGE] = VERDICT_PARTS,
};

static const unsigned char error_verdicts[] = {
    [NYX_TYPE_ERROR] = VERDICT_YES,
    [NYX_TYPE_STRUCT] = VERDICT_PARTS,
    [NYX_TYPE_UNION] = VERDICT_PARTS,
    [NYX_TYPE_HANDLE] = VERDICT_PARTS,
    [NYX_TYPE_LIST] = VERDICT_PARTS,
    [NYX_TYPE_FIXED_LIST] = VERDICT_PARTS,
    [NYX_TYPE_SLICE] = VERDICT_PARTS,
    [NYX_TYPE_RECORD] = VERDICT_PARTS,
    [NYX_TYPE_TABLE] = VERDICT_PARTS,
};

/* What a handle's payload may be: fixed-size data, or handles to more of it.
   Unlike the others it follows handle edges, so it is checked as a graph. */
static const unsigned char payload_verdicts[] = {
    NUMERIC_KINDS(VERDICT_YES),
    [NYX_TYPE_BOOL] = VERDICT_YES,
    [NYX_TYPE_NIL] = VERDICT_YES,
    [NYX_TYPE_STRING] = VERDICT_YES,
    [NYX_TYPE_DYNAMIC_STRING] = VERDICT_YES,
    [NYX_TYPE_FUNCTION] = VERDICT_YES,
    [NYX_TYPE_ERROR] = VERDICT_YES,
    [NYX_TYPE_RANGE] = VERDICT_YES,
    [NYX_TYPE_HANDLE] = VERDICT_PARTS,
    [NYX_TYPE_FIXED_LIST] = VERDICT_PARTS,
    [NYX_TYPE_UNION] = VERDICT_PARTS,
    [NYX_TYPE_STRUCT] = VERDICT_PARTS,
};

/* Types that == can compare. */
static const unsigned char equality_verdicts[] = {
    NUMERIC_KINDS(VERDICT_YES),
    [NYX_TYPE_BOOL] = VERDICT_YES,
    [NYX_TYPE_STRING] = VERDICT_YES,
    [NYX_TYPE_DYNAMIC_STRING] = VERDICT_YES,
    [NYX_TYPE_HANDLE] = VERDICT_YES,
    [NYX_TYPE_FIXED_LIST] = VERDICT_PARTS,
    [NYX_TYPE_LIST] = VERDICT_PARTS,
    [NYX_TYPE_RECORD] = VERDICT_PARTS,
    [NYX_TYPE_TABLE] = VERDICT_PARTS,
    [NYX_TYPE_STRUCT] = VERDICT_PARTS,
    [NYX_TYPE_UNION] = VERDICT_PARTS,
};

static const PropertyRules printable_rules = RULES(printable_verdicts, false);
static const PropertyRules sendable_rules = RULES(sendable_verdicts, false);
static const PropertyRules equality_rules = RULES(equality_verdicts, false);
static const PropertyRules error_rules = RULES(error_verdicts, true);
static const PropertyRules payload_rules = RULES(payload_verdicts, false);

bool is_printable_type(const TypedProgram *program, size_t type_index)
{
    return type_has_property(program, &printable_rules, type_index,
                             NYX_TYPE_DEPTH);
}

bool equality_type_supported(const TypedProgram *program, size_t type_index,
                             unsigned depth)
{
    return type_has_property(program, &equality_rules, type_index, depth);
}

bool type_is_sendable(const TypedProgram *program, size_t type_index)
{
    return type_has_property(program, &sendable_rules, type_index,
                             NYX_TYPE_DEPTH);
}

bool type_contains_error(const TypedProgram *program, size_t type_index)
{
    return type_has_property(program, &error_rules, type_index,
                             NYX_TYPE_DEPTH);
}

/* One step of the payload walk: queues the parts of `type_index` the first
   time it is seen. Returns false for a type that cannot be a payload. */
static bool payload_visit(const TypedProgram *program, size_t type_index,
                          unsigned char *seen, List *pending, bool *memory)
{
    const TypedType *type = typed_program_type(program, type_index);
    Verdict verdict;
    size_t count;
    size_t i;

    if (type == NULL) {
        return false;
    }
    if (seen[type_index]) {
        return true;
    }
    seen[type_index] = 1;
    verdict = verdict_of(&payload_rules, type->kind);
    count = verdict == VERDICT_PARTS ? typed_type_part_count(program, type) : 0;
    for (i = 0; i < count; i++) {
        size_t part;

        if (!typed_type_part(program, type, i, &part)) {
            return false;
        }
        if (!list_push(pending, &part)) {
            *memory = true;
            return false;
        }
    }
    return verdict != VERDICT_NO;
}

/* Payload validation follows handle edges too; recursive handle graphs are
   finite even when their inline layout is recursive through an opaque handle. */
/* Visit the types reachable from `type_index` until one is not a plain value.
   Returns false only when memory ran out; `valid` is the verdict. */
static bool walk_payload(const TypedProgram *program, size_t type_index,
                         unsigned char *seen, size_t count, bool *valid)
{
    List pending;
    bool memory = false;
    bool ok = true;

    list_init(&pending, sizeof(size_t));
    memset(seen, 0, count);
    memory = !list_push(&pending, &type_index);
    while (!memory && ok && pending.length != 0) {
        size_t current;

        memory = !list_pop(&pending, &current);
        ok = !memory && current < count &&
             payload_visit(program, current, seen, &pending, &memory);
    }
    *valid = !memory && ok;
    list_free(&pending);
    return !memory;
}

bool check_handle_payload(CheckContext *ctx, size_t type_index, bool *valid)
{
    const TypedProgram *program = ctx->program;
    size_t count = program->types.length;
    unsigned char *seen = nyx_malloc(count == 0 ? 1 : count);
    bool done = seen != NULL && walk_payload(program, type_index, seen, count, valid);

    *valid = seen != NULL && done && *valid;
    if (!done) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    free(seen);
    return done;
}
