#include "checker_internal.h"

/* Two types are the same exactly when they compare equal, so one engine serves
   both. A kind's rule lists the parts of a TypedType that tell two types of
   that kind apart, in the order they are compared. A kind with no rule has no
   payload (numbers, Bool, Nil, Store, ...) and is equal once the kinds match;
   the checker interns one of each, so that case is the same index. */
typedef int (*TypeStep)(const TypedProgram *program, const TypedType *left,
                        const TypedType *right, unsigned depth);

#define TYPE_RULE_STEPS 2

typedef struct {
    TypeStep steps[TYPE_RULE_STEPS];
} TypeRule;

static int compare_size(size_t left, size_t right)
{
    return left < right ? -1 : (left > right ? 1 : 0);
}

static int compare_runs(const TypedProgram *program, const List *items,
                        size_t left_first, size_t right_first, size_t count,
                        unsigned depth)
{
    size_t i;

    for (i = 0; i < count; i++) {
        int order = type_compare_depth(
            program, *(const size_t *)list_get(items, left_first + i),
            *(const size_t *)list_get(items, right_first + i), depth);

        if (order != 0) {
            return order;
        }
    }
    return 0;
}

static int step_name(const TypedProgram *program, const TypedType *left,
                     const TypedType *right, unsigned depth)
{
    const char *left_name = typed_program_symbol_text(program, left->name_index);
    const char *right_name =
        typed_program_symbol_text(program, right->name_index);

    (void)depth;
    return strcmp(left_name == NULL ? "" : left_name,
                  right_name == NULL ? "" : right_name);
}

static int step_length(const TypedProgram *program, const TypedType *left,
                       const TypedType *right, unsigned depth)
{
    (void)program;
    (void)depth;
    return compare_size(left->fixed_length, right->fixed_length);
}

static int step_children(const TypedProgram *program, const TypedType *left,
                         const TypedType *right, unsigned depth)
{
    int order = compare_size(left->child_count, right->child_count);

    if (order != 0) {
        return order;
    }
    return compare_runs(program, &program->type_children, left->first_child,
                        right->first_child, left->child_count, depth);
}

static int step_signature(const TypedProgram *program, const TypedType *left,
                          const TypedType *right, unsigned depth)
{
    const TypedFunctionSignature *ls =
        typed_program_signature(program, left->signature_index);
    const TypedFunctionSignature *rs =
        typed_program_signature(program, right->signature_index);
    int order;

    if (ls == NULL || rs == NULL) {
        return ls == NULL ? -1 : 1;
    }
    order = compare_size(ls->parameter_count, rs->parameter_count);
    if (order == 0) {
        order = compare_runs(program, &program->signature_parameters,
                             ls->first_parameter, rs->first_parameter,
                             ls->parameter_count, depth);
    }
    if (order == 0) {
        order = type_compare_depth(program, ls->return_type, rs->return_type,
                                   depth);
    }
    return order;
}

/* The order of the steps is the order types sort in, so it must not change. */
static const TypeRule type_rules[] = {
    [NYX_TYPE_STRING] = {{step_length}},
    [NYX_TYPE_STRUCT] = {{step_name, step_children}},
    [NYX_TYPE_FUNCTION] = {{step_signature}},
    [NYX_TYPE_UNION] = {{step_children}},
    [NYX_TYPE_HANDLE] = {{step_children}},
    [NYX_TYPE_CHANNEL] = {{step_children}},
    [NYX_TYPE_LIST] = {{step_children}},
    [NYX_TYPE_FIXED_LIST] = {{step_children, step_length}},
    [NYX_TYPE_SLICE] = {{step_children}},
    [NYX_TYPE_RANGE] = {{step_children}},
    [NYX_TYPE_RECORD] = {{step_length, step_children}},
    [NYX_TYPE_TABLE] = {{step_children}},
};

int type_rank(NyxType kind)
{
    /* The position of a kind is its rank; this order decides how a union's
       alternatives are sorted, so it must not change. */
    static const NyxType order[] = {
        NYX_TYPE_INT32, NYX_TYPE_BOOL, NYX_TYPE_NIL, NYX_TYPE_STRUCT,
        NYX_TYPE_UNION, NYX_TYPE_FUNCTION, NYX_TYPE_BYTE,
        NYX_TYPE_STORE, NYX_TYPE_HANDLE, NYX_TYPE_STRING,
        NYX_TYPE_DYNAMIC_STRING, NYX_TYPE_LIST, NYX_TYPE_FIXED_LIST,
        NYX_TYPE_SLICE, NYX_TYPE_TEXT_BUILDER, NYX_TYPE_STRING_LITERAL,
        NYX_TYPE_ERROR, NYX_TYPE_RECORD, NYX_TYPE_TABLE,
        NYX_TYPE_INT8, NYX_TYPE_INT16, NYX_TYPE_INT64,
        NYX_TYPE_UINT16, NYX_TYPE_UINT32, NYX_TYPE_UINT64,
        NYX_TYPE_FLOAT32, NYX_TYPE_FLOAT64, NYX_TYPE_RANGE,
        NYX_TYPE_CHANNEL,
    };
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(order); i++) {
        if (order[i] == kind) {
            return (int)i;
        }
    }
    return (int)NYX_COUNT_OF(order);
}



/* Compares by kind rank first, then by the kind's rule. Running out of depth
   counts as different so that same_type stays conservative. */
/* The kind's own rule: its steps in turn until one tells the types apart. */
static int compare_by_rule(const TypedProgram *program, const TypedType *left,
                           const TypedType *right, unsigned depth)
{
    const TypeRule *rule = &type_rules[left->kind];
    int order = 0;
    size_t i;

    for (i = 0; i < TYPE_RULE_STEPS && rule->steps[i] != NULL && order == 0;
         i++) {
        order = rule->steps[i](program, left, right, depth);
    }
    return order;
}

/* Compares by kind rank first, then by the kind's rule. Running out of depth
   counts as different so that same_type stays conservative. */
int type_compare_depth(const TypedProgram *program, size_t a, size_t b,
                       unsigned depth)
{
    const TypedType *left = typed_program_type(program, a);
    const TypedType *right = typed_program_type(program, b);
    int order;

    if (a == b) {
        return 0;
    }
    if (left == NULL || right == NULL) {
        return left == NULL ? -1 : 1;
    }
    if (depth == 0) {
        return 1;
    }
    order = compare_size((size_t)type_rank(left->kind),
                         (size_t)type_rank(right->kind));
    if (order != 0 || (size_t)left->kind >= NYX_COUNT_OF(type_rules)) {
        return order;
    }
    return compare_by_rule(program, left, right, depth - 1);
}

int type_compare(const TypedProgram *program, size_t a, size_t b)
{
    return type_compare_depth(program, a, b, NYX_TYPE_DEPTH);
}

bool same_type_depth(const TypedProgram *program, size_t a, size_t b,
                     unsigned depth)
{
    return type_compare_depth(program, a, b, depth) == 0;
}

bool same_type(const TypedProgram *program, size_t a, size_t b)
{
    return same_type_depth(program, a, b, NYX_TYPE_DEPTH);
}

bool type_in_list(const TypedProgram *program, const List *indices,
                         size_t type_index)
{
    size_t i;

    for (i = 0; i < indices->length; i++) {
        size_t existing = *(const size_t *)list_get(indices, i);

        if (same_type(program, existing, type_index)) {
            return true;
        }
    }
    return false;
}

/* Adds `alternative` unless an equal type is already collected. */
static bool collect_alternative(CheckContext *ctx, List *unique,
                                size_t alternative)
{
    if (type_in_list(ctx->program, unique, alternative) ||
        list_push(unique, &alternative)) {
        return true;
    }
    error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return false;
}

/* Flattens nested unions and drops duplicates. */
static bool collect_alternatives(CheckContext *ctx, const List *alternatives,
                                 List *unique)
{
    size_t i;

    for (i = 0; i < alternatives->length; i++) {
        size_t alternative = *(const size_t *)list_get(alternatives, i);
        const TypedType *type = typed_program_type(ctx->program, alternative);
        size_t j;

        if (type == NULL) {
            error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
            return false;
        }
        if (type->kind != NYX_TYPE_UNION) {
            if (!collect_alternative(ctx, unique, alternative)) {
                return false;
            }
            continue;
        }
        for (j = 0; j < type->child_count; j++) {
            size_t nested = *(const size_t *)list_get(
                &ctx->program->type_children, type->first_child + j);

            if (!collect_alternative(ctx, unique, nested)) {
                return false;
            }
        }
    }
    return true;
}

static void sort_alternatives(const TypedProgram *program, size_t *items,
                              size_t count)
{
    size_t i;

    for (i = 1; i < count; i++) {
        size_t key = items[i];
        size_t j = i;

        while (j > 0 && type_compare(program, items[j - 1], key) > 0) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }
}

/* Reuses an existing union with the same canonical alternatives. */
static bool find_union(const TypedProgram *program, const size_t *items,
                       size_t count, size_t *out_index)
{
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);
        size_t j;

        if (type == NULL || type->kind != NYX_TYPE_UNION ||
            type->child_count != count) {
            continue;
        }
        for (j = 0; j < count; j++) {
            size_t existing = *(const size_t *)list_get(
                &program->type_children, type->first_child + j);

            if (!same_type(program, existing, items[j])) {
                break;
            }
        }
        if (j == count) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static bool add_union(CheckContext *ctx, const size_t *items, size_t count,
                      size_t *out_index)
{
    TypedType type = {0};
    size_t i;

    type.first_child = ctx->program->type_children.length;
    for (i = 0; i < count; i++) {
        if (!typed_program_add_type_child(ctx->program, items[i])) {
            return false;
        }
    }
    type.kind = NYX_TYPE_UNION;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.child_count = count;
    type.declaration_node = TYPED_NO_NODE;
    type.source_name_index = TYPED_NO_NODE;
    return typed_program_add_type(ctx->program, &type, out_index);
}

/* The sorted alternatives of `unique`, interned as a union. */
static bool intern_sorted(CheckContext *ctx, const List *unique,
                          size_t *out_index)
{
    size_t count = unique->length;
    size_t *items = nyx_malloc(count * sizeof(size_t));
    size_t i;
    bool ok = true;

    if (items == NULL) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    for (i = 0; i < count; i++) {
        items[i] = *(const size_t *)list_get(unique, i);
    }
    sort_alternatives(ctx->program, items, count);
    if (!find_union(ctx->program, items, count, out_index) &&
        !add_union(ctx, items, count, out_index)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        ok = false;
    }
    free(items);
    return ok;
}

bool intern_union(CheckContext *ctx, const List *alternatives,
                  size_t *out_index)
{
    List unique;
    bool ok;

    list_init(&unique, sizeof(size_t));
    ok = collect_alternatives(ctx, alternatives, &unique);
    if (ok && unique.length == 0) {
        error_report(ctx->errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE);
        ok = false;
    } else if (ok && unique.length == 1) {
        *out_index = *(const size_t *)list_get(&unique, 0);
    } else if (ok) {
        ok = intern_sorted(ctx, &unique, out_index);
    }
    list_free(&unique);
    return ok;
}

bool union_has_alternative(const TypedProgram *program,
                                  const TypedType *union_type,
                                  size_t wanted, size_t *out_tag)
{
    size_t i;

    for (i = 0; i < union_type->child_count; i++) {
        size_t alternative = *(const size_t *)list_get(
            &program->type_children, union_type->first_child + i);

        if (same_type(program, alternative, wanted)) {
            if (out_tag != NULL) {
                *out_tag = i;
            }
            return true;
        }
    }
    return false;
}

/* Writes the source spelling of a type annotation, for diagnostics. */
typedef struct {
    char *buffer;
    size_t size;
    size_t used;
    const Ast *ast;
} TextOut;

typedef void (*TextWriter)(TextOut *out, const AstNode *node, const char *head);

typedef struct {
    const char *head;
    TextWriter write;
} TextRule;

static void text_put(TextOut *out, const char *text)
{
    type_ast_append(text, out->buffer, out->size, &out->used);
}

static void text_child(TextOut *out, size_t ast_index)
{
    char text[128];

    type_ast_text(out->ast, ast_index, text, sizeof(text));
    text_put(out, text);
}

static void text_number(TextOut *out, const char *format, int value)
{
    char text[48];

    snprintf(text, sizeof(text), format, value);
    text_put(out, text);
}

static void text_children(TextOut *out, const AstNode *node,
                          const char *separator)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        if (i > 0) {
            text_put(out, separator);
        }
        text_child(out, *(const size_t *)list_get(&out->ast->children,
                                                  node->children_start + i));
    }
}

static void write_head(TextOut *out, const AstNode *node, const char *head)
{
    (void)node;
    text_put(out, head);
}

static void write_name(TextOut *out, const AstNode *node, const char *head)
{
    const char *name = ast_symbol_text(out->ast, node->name_index);

    (void)head;
    text_put(out, name == NULL ? MESSAGE_FRAGMENT_VALUE : name);
}

static void write_fixed_string(TextOut *out, const AstNode *node,
                               const char *head)
{
    (void)head;
    text_number(out, "String<%d>", node->integer_value);
}

/* Head<element> or Head<element, length> */
static void write_element(TextOut *out, const AstNode *node, const char *head)
{
    text_put(out, head);
    text_put(out, "<");
    text_child(out, node->left_child);
    if (node->kind == AST_TYPE_FIXED_LIST) {
        text_number(out, ", %d", node->integer_value);
    }
    text_put(out, ">");
}

/* Head<key, value> or Head<key, value, length> */
static void write_pair(TextOut *out, const AstNode *node, const char *head)
{
    text_put(out, head);
    text_put(out, "<");
    text_child(out, node->left_child);
    text_put(out, ", ");
    text_child(out, node->right_child);
    if (node->kind == AST_TYPE_RECORD) {
        text_number(out, ", %d", node->integer_value);
    }
    text_put(out, ">");
}

static void write_application(TextOut *out, const AstNode *node,
                              const char *head)
{
    write_name(out, node, head);
    text_put(out, "<");
    text_children(out, node, ", ");
    text_put(out, ">");
}

/* `Owner::Case`, with the type arguments before the `::` when there are any. */
static void write_case(TextOut *out, const AstNode *node, const char *head)
{
    const char *name = ast_symbol_text(out->ast, node->second_name_index);

    write_name(out, node, head);
    if (node->children_count != 0) {
        text_put(out, "<");
        text_children(out, node, ", ");
        text_put(out, ">");
    }
    text_put(out, "::");
    text_put(out, name == NULL ? MESSAGE_FRAGMENT_VALUE : name);
}

static void write_union(TextOut *out, const AstNode *node, const char *head)
{
    (void)head;
    text_children(out, node, " | ");
}

static const TextRule text_rules[] = {
    [AST_TYPE_NAME] = {NULL, write_name},
    [AST_TYPE_FUNCTION] = {"Fun", write_head},
    [AST_TYPE_FIXED_STRING] = {NULL, write_fixed_string},
    [AST_TYPE_LIST] = {"List", write_element},
    [AST_TYPE_FIXED_LIST] = {"Array", write_element},
    [AST_TYPE_SLICE] = {"Slice", write_element},
    [AST_TYPE_RECORD] = {"Record", write_pair},
    [AST_TYPE_TABLE] = {"Table", write_pair},
    [AST_TYPE_APPLICATION] = {NULL, write_application},
    [AST_TYPE_CASE] = {NULL, write_case},
    [AST_TYPE_UNION] = {NULL, write_union},
};

bool type_ast_text(const Ast *ast, size_t ast_index, char *buffer,
                   size_t size)
{
    const AstNode *node = ast_get_node(ast, ast_index);
    TextOut out = {buffer, size, 0, ast};

    buffer[0] = '\0';
    if (node != NULL && (size_t)node->kind < NYX_COUNT_OF(text_rules) &&
        text_rules[node->kind].write != NULL) {
        text_rules[node->kind].write(&out, node, text_rules[node->kind].head);
    } else {
        text_put(&out, MESSAGE_FRAGMENT_VALUE);
    }
    return true;
}

void type_ast_append(const char *text, char *buffer, size_t size,
                            size_t *used)
{
    size_t length = strlen(text);

    if (*used < size) {
        size_t available = size - *used - 1;

        if (length > available) {
            length = available;
        }
        memcpy(buffer + *used, text, length);
        *used += length;
        buffer[*used] = '\0';
    }
}

