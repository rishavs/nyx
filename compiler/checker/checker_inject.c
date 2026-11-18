#include "checker_internal.h"

/* Giving a checked value the type its context expects.

   A pointer into the typed program (a node or a type) is only valid until the
   next thing is added to it: the array can move. Everything below that calls
   something that adds nodes or types (inject_into itself, push_injection,
   builtin_type, numeric_constant_retype) therefore reads what it needs first
   and fetches the pointers again afterwards. `make test-poison` finds a
   mistake here at once.

   The coercions are rules tried in order. A rule declines (the next one
   looks), rejects (the value does not fit) or accepts. A value no rule
   accepts is rejected. */

typedef struct {
    CheckContext *ctx;
    size_t value;
    size_t target;
    size_t *out;
} Injection;

typedef enum {
    INJECT_NEXT,
    INJECT_FAIL,
    INJECT_DONE
} InjectStep;

static TypedNode *injected_node(const Injection *call)
{
    return (TypedNode *)typed_program_node(call->ctx->program, call->value);
}

static const TypedType *injected_target(const Injection *call)
{
    return typed_program_type(call->ctx->program, call->target);
}

static InjectStep inject_result(bool ok)
{
    return ok ? INJECT_DONE : INJECT_FAIL;
}

/* A string literal takes the type its context gives it. The result of
   base64() has its type fixed and must stay a Text. */
static bool is_contextual_string(const TypedNode *node)
{
    return node->kind == TYPED_STRING_LITERAL && !node->fixed_type;
}

/* ---- record and list literals ---- */

static bool key_bytes_equal(const TypedProgram *program, const TypedNode *a,
                            const TypedNode *b)
{
    const unsigned char *bytes = program->bytes.items;

    return a != NULL && a->string_length == b->string_length &&
           memcmp(bytes + a->string_offset, bytes + b->string_offset,
                  b->string_length) == 0;
}

/* A record key must be a string literal that no earlier entry used. */
static bool record_key_is_new(CheckContext *ctx, size_t key, const List *values)
{
    const TypedProgram *program = ctx->program;
    const TypedNode *node = typed_program_node(program, key);
    size_t j;

    if (node == NULL ||
        node->string_offset + node->string_length > program->bytes.length) {
        return false;
    }
    for (j = 0; j < values->length; j += 2) {
        size_t previous = *(const size_t *)list_get(values, j);

        if (key_bytes_equal(program, typed_program_node(program, previous),
                            node)) {
            error_report_at(ctx->errors, node->pos,
                            MESSAGE_CHECKER_RECORD_DUPLICATE_KEY);
            return false;
        }
    }
    return true;
}

/* Coerce one entry and keep the pair; any failure drops the pairs so far. */
static bool inject_entry(CheckContext *ctx, size_t key, size_t key_type,
                         size_t item, size_t value_type, List *values)
{
    size_t coerced_key;
    size_t coerced_value;
    bool ok = inject_into(ctx, key, key_type, &coerced_key) &&
              inject_into(ctx, item, value_type, &coerced_value) &&
              record_key_is_new(ctx, coerced_key, values) &&
              checker_ok(ctx->errors, list_push(values, &coerced_key),
                         MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING) &&
              checker_ok(ctx->errors, list_push(values, &coerced_value),
                         MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);

    if (!ok) {
        list_free(values);
    }
    return ok;
}

/* The target of a record literal must be a record with room for every entry. */
static bool record_target_fits(CheckContext *ctx, const TypedNode *node,
                               const TypedType *record)
{
    if (record->kind != NYX_TYPE_RECORD || record->child_count != 2) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
        return false;
    }
    if (node->children_count % 2 != 0 ||
        node->children_count / 2 > record->fixed_length) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_RECORD_CAPACITY);
        return false;
    }
    return true;
}

static bool inject_record_entries(const Injection *call)
{
    CheckContext *ctx = call->ctx;
    TypedProgram *program = ctx->program;
    const TypedNode *node = injected_node(call);
    const TypedType *record = injected_target(call);
    size_t entries = node->children_count / 2;
    size_t literal_start = node->children_start;
    size_t key_type;
    size_t value_type;
    size_t children_start;
    List values;
    size_t i;

    if (!record_target_fits(ctx, node, record)) {
        return false;
    }
    key_type = *(const size_t *)list_get(&program->type_children,
                                         record->first_child);
    value_type = *(const size_t *)list_get(&program->type_children,
                                           record->first_child + 1);
    children_start = program->children.length;
    list_init(&values, sizeof(size_t));
    for (i = 0; i < entries; i++) {
        size_t key = *(const size_t *)list_get(&program->children,
                                               literal_start + i * 2);
        size_t item = *(const size_t *)list_get(&program->children,
                                                literal_start + i * 2 + 1);

        if (!inject_entry(ctx, key, key_type, item, value_type, &values)) {
            return false;
        }
    }
    for (i = 0; i < values.length; i++) {
        checker_ok(ctx->errors,
                   typed_program_push_child(
                       program, *(const size_t *)list_get(&values, i)),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    }
    if (!ctx->errors->fatal) {
        TypedNode *literal = injected_node(call);

        literal->type_index = call->target;
        literal->children_start = children_start;
        literal->children_count = values.length;
        *call->out = call->value;
    }
    list_free(&values);
    return !ctx->errors->fatal;
}

static InjectStep inject_record_literal(const Injection *call)
{
    if (injected_node(call)->kind != TYPED_RECORD_LITERAL) {
        return INJECT_NEXT;
    }
    return inject_result(inject_record_entries(call));
}

/* Each element of a list literal is coerced to the array's element type and
   the coerced elements are appended as the literal's new children. */
static bool inject_elements(CheckContext *ctx, const TypedType *fixed,
                            const TypedNode *node)
{
    size_t count = node->children_count;
    size_t start = node->children_start;
    size_t element_type;
    size_t i;

    if (fixed->child_count != 1) {
        return false;
    }
    if (count > fixed->fixed_length) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_LIST_LITERAL_LENGTH, count,
                        (int)fixed->fixed_length);
        return false;
    }
    element_type = *(const size_t *)list_get(&ctx->program->type_children,
                                             fixed->first_child);
    for (i = 0; i < count; i++) {
        size_t child = *(const size_t *)list_get(&ctx->program->children,
                                                 start + i);
        size_t coerced;

        if (!inject_into(ctx, child, element_type, &coerced) ||
            !typed_program_push_child(ctx->program, coerced)) {
            if (!ctx->errors->reported) {
                error_report(ctx->errors, MESSAGE_CHECKER_LIST_VALUE);
            }
            return false;
        }
    }
    return true;
}

static InjectStep inject_list_literal(const Injection *call)
{
    CheckContext *ctx = call->ctx;
    const TypedNode *node = injected_node(call);
    size_t children_start = ctx->program->children.length;
    TypedNode *literal;

    if (node->kind != TYPED_LIST_LITERAL) {
        return INJECT_NEXT;
    }
    if (injected_target(call)->kind != NYX_TYPE_FIXED_LIST) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_LIST_LITERAL_CONTEXT);
        return INJECT_FAIL;
    }
    if (!inject_elements(ctx, injected_target(call), node)) {
        return INJECT_FAIL;
    }
    literal = injected_node(call);
    literal->type_index = call->target;
    literal->children_start = children_start;
    *call->out = call->value;
    return INJECT_DONE;
}

/* ---- scalars ---- */

static InjectStep inject_same_type(const Injection *call)
{
    if (!same_type(call->ctx->program, injected_node(call)->type_index,
                   call->target)) {
        return INJECT_NEXT;
    }
    *call->out = call->value;
    return INJECT_DONE;
}

/* A literal, or a negated literal, takes the numeric type it is assigned to.
   This is literal typing, not a conversion: the result of an explicit
   conversion keeps the type it was given. */
static InjectStep inject_numeric_literal(const Injection *call)
{
    CheckContext *ctx = call->ctx;
    const TypedType *target = injected_target(call);
    NumericConstant constant;

    if (!nyx_type_is_numeric(target->kind)) {
        return INJECT_NEXT;
    }
    if (!numeric_constant_read(ctx->program, call->value, false, &constant) ||
        constant.is_float != nyx_type_is_float(target->kind) ||
        !numeric_constant_check_range(ctx, injected_node(call)->pos, &constant,
                                      target->kind) ||
        !numeric_constant_retype(ctx, call->value, call->target, target->kind)) {
        return INJECT_FAIL;
    }
    *call->out = call->value;
    return INJECT_DONE;
}

static InjectStep inject_dynamic_string(const Injection *call)
{
    TypedNode *node = injected_node(call);

    if (injected_target(call)->kind != NYX_TYPE_DYNAMIC_STRING) {
        return INJECT_NEXT;
    }
    if (!is_contextual_string(node)) {
        return INJECT_FAIL;
    }
    node->type_index = call->target;
    *call->out = call->value;
    return INJECT_DONE;
}

static InjectStep inject_fixed_string(const Injection *call)
{
    TypedNode *node = injected_node(call);
    const TypedType *target = injected_target(call);

    if (target->kind != NYX_TYPE_STRING) {
        return INJECT_NEXT;
    }
    if (!is_contextual_string(node)) {
        return INJECT_FAIL;
    }
    if (node->string_length > target->fixed_length) {
        error_report_at(call->ctx->errors, node->pos,
                        MESSAGE_CHECKER_STRING_TOO_LONG, node->string_length,
                        (int)target->fixed_length);
        return INJECT_FAIL;
    }
    node->type_index = call->target;
    *call->out = call->value;
    return INJECT_DONE;
}

/* ---- unions ---- */

typedef struct {
    size_t fitting;        /* the alternative a literal would go to */
    size_t count;          /* how many alternatives could hold it */
    size_t first_string;   /* first fixed string alternative, for the error */
    size_t first_capacity;
} StringFit;

/* A string literal fits any Text alternative and every fixed string it is no
   longer than. */
static void scan_string_alternatives(const TypedProgram *program,
                                     const TypedType *target, size_t length,
                                     StringFit *fit)
{
    size_t i;

    *fit = (StringFit){TYPED_NO_NODE, 0, TYPED_NO_NODE, 0};
    for (i = 0; i < target->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, target->first_child + i);
        const TypedType *type = typed_program_type(program, alternative);
        bool text = type != NULL && type->kind == NYX_TYPE_DYNAMIC_STRING;
        bool fixed = type != NULL && type->kind == NYX_TYPE_STRING;

        if (fixed && fit->first_string == TYPED_NO_NODE) {
            fit->first_string = alternative;
            fit->first_capacity = type->fixed_length;
        }
        if (text || (fixed && length <= type->fixed_length)) {
            fit->fitting = alternative;
            fit->count++;
        }
    }
}

/* Put the value into the union alternative `alternative`. */
static InjectStep inject_alternative(const Injection *call, size_t alternative)
{
    const TypedType *target = injected_target(call);
    size_t tag;

    if (!union_has_alternative(call->ctx->program, target, alternative, &tag)) {
        return INJECT_FAIL;
    }
    return inject_result(push_injection(call->ctx, call->value, call->target,
                                        tag, call->out));
}

/* A string literal in a union goes to the one alternative that can hold it.
   Two fitting alternatives are ambiguous. */
static InjectStep inject_union_string(const Injection *call)
{
    CheckContext *ctx = call->ctx;
    TypedNode *node = injected_node(call);
    StringFit fit;

    scan_string_alternatives(ctx->program, injected_target(call),
                             node->string_length, &fit);
    if (fit.count > 1) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_STRING_AMBIGUOUS);
        return INJECT_FAIL;
    }
    if (fit.count == 1) {
        node->type_index = fit.fitting;
        return inject_alternative(call, fit.fitting);
    }
    if (fit.first_string == TYPED_NO_NODE) {
        return INJECT_NEXT;
    }
    error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_STRING_TOO_LONG,
                    node->string_length, (int)fit.first_capacity);
    return INJECT_FAIL;
}

static InjectStep inject_union(const Injection *call)
{
    const TypedNode *node = injected_node(call);
    InjectStep step = INJECT_NEXT;
    size_t tag;

    if (injected_target(call)->kind != NYX_TYPE_UNION) {
        return INJECT_FAIL;
    }
    if (is_contextual_string(node)) {
        step = inject_union_string(call);
    }
    if (step != INJECT_NEXT) {
        return step;
    }
    if (!union_has_alternative(call->ctx->program, injected_target(call),
                               node->type_index, &tag)) {
        return INJECT_NEXT;
    }
    return inject_result(push_injection(call->ctx, call->value, call->target,
                                        tag, call->out));
}

typedef struct {
    size_t fitting;
    size_t count;
    NyxType kind;        /* of the fitting alternative */
    NyxType first_kind;  /* of the first alternative of the constant's sort */
} NumericFit;

static bool constant_fits(const NumericConstant *constant, NyxType kind)
{
    return constant->is_float
               ? float_constant_fits(kind, constant->real)
               : integer_constant_fits(kind, constant->negative,
                                       constant->magnitude);
}

static void scan_numeric_alternatives(const TypedProgram *program,
                                      const TypedType *target,
                                      const NumericConstant *constant,
                                      NumericFit *fit)
{
    size_t i;

    *fit = (NumericFit){TYPED_NO_NODE, 0, NYX_TYPE_UNKNOWN, NYX_TYPE_UNKNOWN};
    for (i = 0; i < target->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, target->first_child + i);
        const TypedType *type = typed_program_type(program, alternative);

        if (type == NULL || !nyx_type_is_numeric(type->kind) ||
            constant->is_float != nyx_type_is_float(type->kind)) {
            continue;
        }
        if (fit->first_kind == NYX_TYPE_UNKNOWN) {
            fit->first_kind = type->kind;
        }
        if (constant_fits(constant, type->kind)) {
            fit->fitting = alternative;
            fit->kind = type->kind;
            fit->count++;
        }
    }
}

/* A numeric literal in a union goes to the one alternative that can hold it.
   Two fitting alternatives are ambiguous, as for strings; an alternative of
   the right kind that is too small reports its range. */
static InjectStep inject_union_numeric(const Injection *call)
{
    CheckContext *ctx = call->ctx;
    SourcePos pos = injected_node(call)->pos;
    NumericConstant constant;
    NumericFit fit;

    if (!numeric_constant_read(ctx->program, call->value, false, &constant)) {
        return INJECT_NEXT;
    }
    scan_numeric_alternatives(ctx->program, injected_target(call), &constant,
                              &fit);
    if (fit.count > 1) {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_NUMERIC_AMBIGUOUS);
        return INJECT_FAIL;
    }
    if (fit.count == 1) {
        /* Retyping can add a type, which may move the type array. */
        return numeric_constant_retype(ctx, call->value, fit.fitting, fit.kind)
                   ? inject_alternative(call, fit.fitting)
                   : INJECT_FAIL;
    }
    if (fit.first_kind != NYX_TYPE_UNKNOWN) {
        (void)numeric_constant_check_range(ctx, pos, &constant, fit.first_kind);
        return INJECT_FAIL;
    }
    return INJECT_NEXT;
}

typedef InjectStep (*InjectRule)(const Injection *call);

static const InjectRule inject_rules[] = {
    inject_record_literal, inject_list_literal, inject_same_type,
    inject_numeric_literal, inject_dynamic_string, inject_fixed_string,
    inject_union, inject_union_numeric,
};

bool inject_into(CheckContext *ctx, size_t value, size_t target,
                 size_t *out_value)
{
    Injection call = {ctx, value, target, out_value};
    size_t i;

    if (injected_node(&call) == NULL || injected_target(&call) == NULL) {
        return false;
    }
    for (i = 0; i < NYX_COUNT_OF(inject_rules); i++) {
        InjectStep step = inject_rules[i](&call);

        if (step != INJECT_NEXT) {
            return step == INJECT_DONE;
        }
    }
    return false;
}
