#include "checker_internal.h"

#include <limits.h>
#include <stdint.h>


bool literal_int_value(const Ast *ast, size_t ast_index, int *out)
{
    const AstNode *node = ast_get_node(ast, ast_index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == AST_INTEGER_LITERAL) {
        if (node->integer_value_u > (uint64_t)INT_MAX) {
            return false;
        }
        *out = node->integer_value;
        return true;
    }
    if (node->kind == AST_UNARY && node->op == NYX_OPERATOR_NEG) {
        const AstNode *operand = ast_get_node(ast, node->left_child);

        if (operand != NULL && operand->kind == AST_INTEGER_LITERAL) {
            *out = -operand->integer_value;
            return true;
        }
    }
    return false;
}

bool check_negative_capacity(CheckContext *ctx, size_t capacity_ast)
{
    bool is_constant = false;
    int value = 0;

    if (!const_int_eval(ctx, capacity_ast, &value, &is_constant)) {
        return false;
    }
    if (!is_constant || value >= 0) {
        return true;
    }
    error_report_at(ctx->errors,
                    ast_get_node(ctx->ast, capacity_ast)->pos,
                    MESSAGE_CHECKER_NEGATIVE_CAPACITY);
    return false;
}


static bool const_value_of_name(CheckContext *ctx, const char *name,
                                int *out)
{
    const VariableEntry *variable = find_variable(ctx->locals, name);

    if (variable == NULL && !ctx->in_function) {
        variable = find_variable(ctx->globals, name);
    }
    if (variable == NULL || !variable->has_constant) {
        return false;
    }
    *out = variable->constant_value;
    return true;
}


/* Constant folding of integer expressions. An expression is folded in the type
   it has: the numbers it may take (its domain) are those of that type, so a sum
   that is out of range for Int32 but fine for Int64 folds in an Int64
   expression. Two tables drive it: which node kinds can be constant and how to
   read each, and which operators fold and what each does to two constants. A
   fold reports a fault instead of a value when the C operation would be
   undefined, and the fault names the message.

   A value outside the domain is an error in Int32, the type the language folds
   to report overflow (see binary_classify); in any other type it only means
   the expression is not known at compile time, and the run-time check
   decides. */
typedef enum {
    FOLD_OK,
    FOLD_OVERFLOW,
    FOLD_ZERO_DIVISOR,
    FOLD_SHIFT_RANGE
} FoldFault;

static const char *const fold_fault_messages[] = {
    [FOLD_OVERFLOW] = MESSAGE_CHECKER_CONST_OVERFLOW,
    [FOLD_ZERO_DIVISOR] = MESSAGE_CHECKER_ZERO_DIVISOR,
    [FOLD_SHIFT_RANGE] = MESSAGE_CHECKER_SHIFT_RANGE
};

/* The values of an integer type, as far as they fit in an int64_t. */
typedef struct {
    NyxType kind;
    int64_t min;
    int64_t max;
    unsigned bits;
    bool is_signed;
} FoldDomain;

static bool fold_domain(NyxType kind, FoldDomain *domain)
{
    if (!nyx_type_is_integer(kind)) {
        return false;
    }
    domain->kind = kind;
    domain->bits = nyx_type_bit_width(kind);
    domain->is_signed = nyx_type_is_signed_integer(kind);
    if (domain->is_signed) {
        domain->max = domain->bits == 64
                          ? INT64_MAX : ((int64_t)1 << (domain->bits - 1)) - 1;
        domain->min = -domain->max - 1;
    } else {
        domain->min = 0;
        domain->max = domain->bits >= 63
                          ? INT64_MAX : ((int64_t)1 << domain->bits) - 1;
    }
    return true;
}

typedef FoldFault (*ConstFold)(int64_t left, int64_t right,
                               const FoldDomain *domain, int64_t *out);

static FoldFault fold_fit(int64_t value, const FoldDomain *domain,
                          int64_t *out)
{
    if (value < domain->min || value > domain->max) {
        return FOLD_OVERFLOW;
    }
    *out = value;
    return FOLD_OK;
}

static FoldFault fold_add(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    int64_t sum;

    return __builtin_add_overflow(left, right, &sum)
               ? FOLD_OVERFLOW : fold_fit(sum, domain, out);
}

static FoldFault fold_sub(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    int64_t difference;

    return __builtin_sub_overflow(left, right, &difference)
               ? FOLD_OVERFLOW : fold_fit(difference, domain, out);
}

static FoldFault fold_mul(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    int64_t product;

    return __builtin_mul_overflow(left, right, &product)
               ? FOLD_OVERFLOW : fold_fit(product, domain, out);
}

static FoldFault fold_div(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    if (right == 0) return FOLD_ZERO_DIVISOR;
    if (left == INT64_MIN && right == -1) return FOLD_OVERFLOW;
    return fold_fit(left / right, domain, out);
}

static FoldFault fold_mod(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    if (right == 0) return FOLD_ZERO_DIVISOR;
    return fold_fit(left == INT64_MIN && right == -1 ? 0 : left % right, domain,
                    out);
}

static FoldFault fold_and(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    return fold_fit(left & right, domain, out);
}

static FoldFault fold_or(int64_t left, int64_t right,
                         const FoldDomain *domain, int64_t *out)
{
    return fold_fit(left | right, domain, out);
}

static FoldFault fold_xor(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    return fold_fit(left ^ right, domain, out);
}

static FoldFault fold_shl(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    uint64_t shifted;

    if (right < 0 || right >= (int64_t)domain->bits) return FOLD_SHIFT_RANGE;
    shifted = (uint64_t)left << right;
    if (domain->bits < 64) {
        shifted &= ((uint64_t)1 << domain->bits) - 1;
        /* Bits shifted past the top are dropped, as the C operation does on the
           unsigned value, and a signed result takes its sign from the top bit. */
        if (domain->is_signed && (shifted >> (domain->bits - 1)) != 0) {
            shifted |= ~(((uint64_t)1 << domain->bits) - 1);
        }
    } else if (!domain->is_signed && shifted > (uint64_t)INT64_MAX) {
        return FOLD_OVERFLOW;
    }
    *out = (int64_t)shifted;
    return FOLD_OK;
}

static FoldFault fold_shr(int64_t left, int64_t right,
                          const FoldDomain *domain, int64_t *out)
{
    if (right < 0 || right >= (int64_t)domain->bits) return FOLD_SHIFT_RANGE;
    *out = left >> right;
    return FOLD_OK;
}

/* The operators that fold, with the spelling a zero-divisor message shows. */
static const struct {
    ConstFold fold;
    const char *symbol;
} const_folds[] = {
    [NYX_OPERATOR_ADD] = {fold_add, "+"},
    [NYX_OPERATOR_SUB] = {fold_sub, "-"},
    [NYX_OPERATOR_MUL] = {fold_mul, "*"},
    [NYX_OPERATOR_DIV] = {fold_div, "/"},
    [NYX_OPERATOR_MOD] = {fold_mod, "%"},
    [NYX_OPERATOR_BIT_AND] = {fold_and, "&"},
    [NYX_OPERATOR_BIT_OR] = {fold_or, "|"},
    [NYX_OPERATOR_BIT_XOR] = {fold_xor, "^"},
    [NYX_OPERATOR_SHL] = {fold_shl, "<<"},
    [NYX_OPERATOR_SHR] = {fold_shr, ">>"}
};

static bool const_int_eval_node(CheckContext *ctx, size_t ast_index,
                                const FoldDomain *domain, int64_t *out,
                                bool *is_constant);

/* A reader for one kind of node: it sets `*is_constant` and `*out`, and
   returns false only after reporting an error. */
typedef bool (*ConstRead)(CheckContext *ctx, const AstNode *node,
                          size_t ast_index, const FoldDomain *domain,
                          int64_t *out, bool *is_constant);

/* A fault is an error in Int32; in another type it only makes the value
   unknown, except a zero divisor or a shift count, which are wrong in any. */
static bool report_fault(CheckContext *ctx, const AstNode *node,
                         const FoldDomain *domain, FoldFault fault,
                         const char *symbol, bool *is_constant)
{
    if (fault == FOLD_OVERFLOW && domain->kind != NYX_TYPE_INT32) {
        *is_constant = false;
        return true;
    }
    error_report_at(ctx->errors, node->pos, fold_fault_messages[fault], symbol);
    return false;
}

static bool read_literal(CheckContext *ctx, const AstNode *node,
                         size_t ast_index, const FoldDomain *domain,
                         int64_t *out, bool *is_constant)
{
    (void)ctx;
    (void)ast_index;
    *is_constant = node->integer_value_u <= (uint64_t)domain->max;
    if (*is_constant) *out = (int64_t)node->integer_value_u;
    return true;
}

static bool read_name(CheckContext *ctx, const AstNode *node,
                      size_t ast_index, const FoldDomain *domain, int64_t *out,
                      bool *is_constant)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    int value = 0;

    (void)ast_index;
    *is_constant = name != NULL && const_value_of_name(ctx, name, &value) &&
                   value >= domain->min && value <= domain->max;
    if (*is_constant) *out = value;
    return true;
}

static bool read_unary(CheckContext *ctx, const AstNode *node,
                       size_t ast_index, const FoldDomain *domain,
                       int64_t *out, bool *is_constant)
{
    int64_t operand;

    (void)ast_index;
    if (node->op != NYX_OPERATOR_NEG) {
        *is_constant = false;
        return true;
    }
    if (!const_int_eval_node(ctx, node->left_child, domain, &operand,
                             is_constant)) {
        return false;
    }
    if (!*is_constant) return true;
    if (operand == INT64_MIN || -operand < domain->min ||
        -operand > domain->max) {
        return report_fault(ctx, node, domain, FOLD_OVERFLOW, NULL,
                            is_constant);
    }
    *out = -operand;
    return true;
}

static bool fold_binary(CheckContext *ctx, const AstNode *node,
                        const FoldDomain *domain, int64_t *out,
                        bool *is_constant);

/* A binary node's fold is remembered while the outermost binary expression
   around it is being checked, which is when the same node is folded again by
   each expression that contains it. The memo belongs to one type: a node folded
   in another is folded again. */
static bool read_binary(CheckContext *ctx, const AstNode *node,
                        size_t ast_index, const FoldDomain *domain,
                        int64_t *out, bool *is_constant)
{
    FoldMemo *memo = ctx->binary_depth > 0
                         ? (FoldMemo *)list_get(ctx->fold_memo, ast_index)
                         : NULL;

    if (memo != NULL && memo->stamp == ctx->fold_epoch &&
        memo->kind == domain->kind) {
        *is_constant = memo->is_constant;
        *out = memo->value;
        return true;
    }
    if (!fold_binary(ctx, node, domain, out, is_constant)) {
        return false;
    }
    if (memo != NULL) {
        memo->stamp = ctx->fold_epoch;
        memo->kind = domain->kind;
        memo->is_constant = *is_constant;
        memo->value = *is_constant ? *out : 0;
    }
    return true;
}

static bool fold_binary(CheckContext *ctx, const AstNode *node,
                        const FoldDomain *domain, int64_t *out,
                        bool *is_constant)
{
    int64_t left, right;
    bool left_constant, right_constant;
    FoldFault fault;
    size_t op = (size_t)node->op;

    /* Both sides are read even when the first is not constant, so an error in
       either is reported. */
    if (!const_int_eval_node(ctx, node->left_child, domain, &left,
                             &left_constant) ||
        !const_int_eval_node(ctx, node->right_child, domain, &right,
                             &right_constant)) {
        return false;
    }
    *is_constant = false;
    if (!left_constant || !right_constant ||
        op >= sizeof(const_folds) / sizeof(const_folds[0]) ||
        const_folds[op].fold == NULL) {
        return true;
    }
    fault = const_folds[op].fold(left, right, domain, out);
    if (fault != FOLD_OK) {
        return report_fault(ctx, node, domain, fault, const_folds[op].symbol,
                            is_constant);
    }
    *is_constant = true;
    return true;
}

static const struct {
    AstNodeKind kind;
    ConstRead read;
} const_reads[] = {
    {AST_INTEGER_LITERAL, read_literal},
    {AST_IDENTIFIER, read_name},
    {AST_UNARY, read_unary},
    {AST_BINARY, read_binary}
};

static bool const_int_eval_node(CheckContext *ctx, size_t ast_index,
                                const FoldDomain *domain, int64_t *out,
                                bool *is_constant)
{
    const AstNode *node = ast_get_node(ctx->ast, ast_index);
    size_t i;

    if (node == NULL) {
        error_report(ctx->errors, MESSAGE_CHECKER_MISSING_AST_NODE, ast_index);
        return false;
    }
    for (i = 0; i < sizeof(const_reads) / sizeof(const_reads[0]); i++) {
        if (const_reads[i].kind == node->kind) {
            return const_reads[i].read(ctx, node, ast_index, domain, out,
                                       is_constant);
        }
    }
    *is_constant = false;
    return true;
}

bool const_int_eval_in(CheckContext *ctx, size_t ast_index, NyxType kind,
                       int64_t *out, bool *is_constant)
{
    FoldDomain domain;

    *is_constant = false;
    if (!fold_domain(kind, &domain)) {
        return true;
    }
    return const_int_eval_node(ctx, ast_index, &domain, out, is_constant);
}


/* The fold in Int32, the type of capacities, sizes, and Int32 constants. */
bool const_int_eval(CheckContext *ctx, size_t ast_index, int *out,
                    bool *is_constant)
{
    int64_t value = 0;

    if (!const_int_eval_in(ctx, ast_index, NYX_TYPE_INT32, &value,
                           is_constant)) {
        return false;
    }
    if (*is_constant) {
        *out = (int)value;
    }
    return true;
}


bool eval_const_int(CheckContext *ctx, size_t ast_index, int *out)
{
    bool is_constant = false;

    if (!const_int_eval(ctx, ast_index, out, &is_constant)) {
        return false;
    }
    if (!is_constant) {
        error_report_at(ctx->errors,
                        ast_get_node(ctx->ast, ast_index)->pos,
                        MESSAGE_CHECKER_STORE_SIZE);
        return false;
    }
    return true;
}

/* The one positional argument of an initialiser that is a call of
   `constructor`; false after saying the initialiser is wrong. */
static bool constructor_argument(CheckContext *ctx, const AstNode *node,
                                 const char *name, const char *constructor,
                                 const char *message, size_t *argument_ast)
{
    const AstNode *initialiser = ast_get_node(ctx->ast, node->left_child);
    const char *called = initialiser == NULL || initialiser->kind != AST_CALL
                             ? NULL
                             : ast_symbol_text(ctx->ast, initialiser->name_index);
    const AstNode *argument;

    if (called == NULL || strcmp(called, constructor) != 0 ||
        initialiser->children_count != 1) {
        error_report_at(ctx->errors, node->pos, message, name);
        return false;
    }
    *argument_ast = *(const size_t *)list_get(&ctx->ast->children,
                                              initialiser->children_start);
    argument = ast_get_node(ctx->ast, *argument_ast);
    if (argument == NULL || argument->kind == AST_NAMED_ARG) {
        error_report_at(ctx->errors, node->pos, message, name);
        return false;
    }
    return true;
}

bool check_store_declaration(CheckContext *ctx, const AstNode *node,
                             const char *name, size_t ast_index,
                             size_t store_type)
{
    StoreEntry entry = {0};
    TypedNode typed;
    size_t size_ast;
    int size;

    if (!constructor_argument(ctx, node, name, "Store",
                              MESSAGE_CHECKER_STORE_INITIALIZER, &size_ast) ||
        !check_negative_capacity(ctx, size_ast) ||
        !eval_const_int(ctx, size_ast, &size)) {
        return false;
    }
    if (find_store(ctx->stores, name) != NULL) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    memcpy(entry.name, name, strlen(name) + 1);
    entry.id = ctx->stores->length;
    entry.initial_size = size;
    checker_ok(ctx->errors, list_push(ctx->stores, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    typed = new_node(TYPED_STORE_DECLARATION, store_type, node->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, name);
    typed.store_id = entry.id;
    typed.integer_value = size;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


/* `global name: Atomic<T> = Atomic(constant)`. T is a Bool or integer type; the
   cell is static storage, so it has no lifetime to track and any thread may use
   it. Its only operations are load, store, and add. */
/* An Atomic holds a Bool or an integer. */
static bool atomic_element_type(CheckContext *ctx, const AstNode *node,
                                const char *name, NyxType *kind,
                                size_t *element_type)
{
    const AstNode *annotation = ast_get_node(ctx->ast, node->right_child);
    const TypedType *element;

    if (annotation->children_count != 1) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ATOMIC_TYPE);
        return false;
    }
    if (!resolve_type_node(ctx, *(const size_t *)list_get(
                                    &ctx->ast->children,
                                    annotation->children_start),
                           name, element_type)) {
        return false;
    }
    element = typed_program_type(ctx->program, *element_type);
    if (element == NULL ||
        (element->kind != NYX_TYPE_BOOL && element->kind != NYX_TYPE_BYTE &&
         !nyx_type_is_integer(element->kind))) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ATOMIC_TYPE);
        return false;
    }
    *kind = element->kind;
    return true;
}

/* The constant an Atomic starts with, as a sign and a magnitude. */
static bool atomic_initial_value(CheckContext *ctx, size_t coerced,
                                 NyxType element_kind, bool *negative,
                                 uint64_t *magnitude)
{
    const TypedNode *literal = typed_program_node(ctx->program, coerced);
    NumericConstant constant;

    if (literal != NULL && literal->kind == TYPED_BOOL_LITERAL &&
        element_kind == NYX_TYPE_BOOL) {
        *negative = false;
        *magnitude = literal->integer_value != 0 ? 1 : 0;
        return true;
    }
    if (element_kind == NYX_TYPE_BOOL ||
        !numeric_constant_read(ctx->program, coerced, true, &constant) ||
        constant.is_float) {
        return false;
    }
    *negative = constant.negative && constant.magnitude != 0;
    *magnitude = constant.magnitude;
    return true;
}

/* `global name: Atomic<T> = Atomic(constant)`. T is a Bool or integer type; the
   cell is static storage, so it has no lifetime to track and any thread may use
   it. Its only operations are load, store, and add. */
/* The constant an Atomic is declared with, coerced to the element type. */
static bool atomic_initial(CheckContext *ctx, const AstNode *node,
                           size_t value_ast, size_t element_type,
                           NyxType element_kind, const char *name,
                           bool *negative, uint64_t *magnitude)
{
    size_t value;
    size_t coerced;

    if (check_expression(ctx, value_ast, &value) &&
        inject_into(ctx, value, element_type, &coerced) &&
        check_default_numeric(ctx, coerced) &&
        atomic_initial_value(ctx, coerced, element_kind, negative, magnitude)) {
        return true;
    }
    if (!ctx->errors->reported) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_ATOMIC_CONSTANT,
                        name);
    }
    return false;
}

bool check_atomic_declaration(CheckContext *ctx, const AstNode *node,
                              const char *name, size_t ast_index)
{
    AtomicEntry entry = {0};
    TypedNode typed;
    NyxType element_kind;
    uint64_t magnitude;
    bool negative;
    size_t element_type;
    size_t value_ast;

    if (!node->is_global || node->is_const) {
        error_report_at(ctx->errors, node->pos,
                        node->is_global ? MESSAGE_CHECKER_CONST_STORE
                                        : MESSAGE_CHECKER_ATOMIC_NOT_GLOBAL);
        return false;
    }
    if (!atomic_element_type(ctx, node, name, &element_kind, &element_type) ||
        !constructor_argument(ctx, node, name, "Atomic",
                              MESSAGE_CHECKER_ATOMIC_INITIALIZER, &value_ast) ||
        !atomic_initial(ctx, node, value_ast, element_type, element_kind,
                        name, &negative, &magnitude)) {
        return false;
    }
    if (name_is_taken(ctx, name, true)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    memcpy(entry.name, name, strlen(name) + 1);
    entry.id = ctx->atomics->length;
    entry.type_index = element_type;
    checker_ok(ctx->errors, list_push(ctx->atomics, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    typed = new_node(TYPED_ATOMIC_DECLARATION, element_type, node->pos,
                     TYPED_NO_NODE, TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, name);
    typed.store_id = entry.id;
    typed.integer_value = negative ? 1 : 0;
    typed.integer_value_u = magnitude;
    return emit_node(ctx, &typed, ast_index, &(size_t){0});
}


/* Whether the written type is an application of the struct called `name`. */
static bool applies_struct(const CheckContext *ctx, const AstNode *type,
                           const char *name)
{
    const char *used = type->kind == AST_TYPE_APPLICATION
                           ? ast_symbol_text(ctx->ast, type->name_index)
                           : NULL;

    return used != NULL && strcmp(used, name) == 0;
}

/* A generic struct may mention itself only as itself, with its own type
   parameters in declaration order. Any other argument list makes each
   instantiation need a larger one, so it is rejected before any use. */
/* An application of the struct itself that is not exactly its own parameters,
   in order. */
static bool application_expands(const CheckContext *ctx, const AstNode *type,
                                const TypeEntry *entry)
{
    size_t i;

    if (type->children_count != entry->parameter_count) {
        return true;
    }
    for (i = 0; i < type->children_count; i++) {
        const AstNode *argument = ast_get_node(
            ctx->ast, *(const size_t *)list_get(&ctx->ast->children,
                                                type->children_start + i));
        const char *argument_name =
            argument == NULL || argument->kind != AST_TYPE_NAME
                ? NULL
                : ast_symbol_text(ctx->ast, argument->name_index);

        if (argument_name == NULL ||
            strcmp(argument_name, entry->parameters[i]) != 0) {
            return true;
        }
    }
    return false;
}

/* A generic struct may mention itself only as itself, with its own type
   parameters in declaration order. Any other argument list makes each
   instantiation need a larger one, so it is rejected before any use. */
static bool self_use_expands(const CheckContext *ctx, size_t type_index,
                             const char *name, const TypeEntry *entry,
                             unsigned depth)
{
    const AstNode *type = ast_get_node(ctx->ast, type_index);
    size_t i;

    if (type == NULL || depth > NYX_MAX_PARSE_NESTING) {
        return false;
    }
    if (applies_struct(ctx, type, name)) {
        return application_expands(ctx, type, entry);
    }
    if (self_use_expands(ctx, type->left_child, name, entry, depth + 1) ||
        self_use_expands(ctx, type->right_child, name, entry, depth + 1)) {
        return true;
    }
    for (i = 0; i < type->children_count; i++) {
        if (self_use_expands(ctx, *(const size_t *)list_get(
                                      &ctx->ast->children,
                                      type->children_start + i),
                             name, entry, depth + 1)) {
            return true;
        }
    }
    return false;
}

/* Declarations that make a resource rather than a variable, told apart by the
   type they are annotated with. */
typedef bool (*ResourceDeclaration)(CheckContext *ctx, const AstNode *node,
                                    const char *name, size_t ast_index);

static const struct {
    const char *annotation;
    ResourceDeclaration check;
} resource_declarations[] = {
    {"Atomic", check_atomic_declaration},
    {"Channel", check_channel_declaration},
};

static ResourceDeclaration resource_declaration_for(const CheckContext *ctx,
                                                    const AstNode *node)
{
    const AstNode *annotation = node->right_child == AST_NO_NODE
                                    ? NULL
                                    : ast_get_node(ctx->ast, node->right_child);
    const char *applied = annotation != NULL &&
                                  annotation->kind == AST_TYPE_APPLICATION
                              ? ast_symbol_text(ctx->ast, annotation->name_index)
                              : NULL;
    size_t i;

    for (i = 0; applied != NULL && i < NYX_COUNT_OF(resource_declarations); i++) {
        if (strcmp(applied, resource_declarations[i].annotation) == 0) {
            return resource_declarations[i].check;
        }
    }
    return NULL;
}

typedef enum {
    DECLARED_VARIABLE,
    DECLARED_REJECTED,
    DECLARED_STORE
} DeclaredKind;

/* A `global` is always a Store (or an Atomic or Channel, handled before); a
   Store is always a `global`, never `const`. */
static DeclaredKind declared_kind(CheckContext *ctx, const AstNode *node,
                                  const char *name, size_t declared_type)
{
    const TypedType *annotated = typed_program_type(ctx->program, declared_type);
    bool store = annotated != NULL && annotated->kind == NYX_TYPE_STORE;

    if (node->is_global && !store) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_GLOBAL_DECLARATION, name);
        return DECLARED_REJECTED;
    }
    if (!store) {
        return DECLARED_VARIABLE;
    }
    if (node->is_const) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_CONST_STORE);
        return DECLARED_REJECTED;
    }
    if (!node->is_global) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_STORE_NOT_GLOBAL,
                        name);
        return DECLARED_REJECTED;
    }
    return DECLARED_STORE;
}

static bool check_global_name(CheckContext *ctx, const AstNode *node,
                              const char *name)
{
    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    if (name_is_taken(ctx, name, true)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_DUPLICATE_NAME,
                        name);
        return false;
    }
    return true;
}

/* The global the declaration makes, with its value if it is a constant. */
static bool register_global(CheckContext *ctx, const AstNode *node,
                            const char *name, size_t variable_type)
{
    VariableEntry entry = {0};

    memcpy(entry.name, name, strlen(name) + 1);
    entry.type_index = variable_type;
    entry.is_const = node->is_const;
    if (!record_constant_value(ctx, node, &entry)) {
        return false;
    }
    checker_ok(ctx->errors, list_push(ctx->globals, &entry),
               MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    return true;
}

/* A top-level declaration names something new and starts from a value. */
static bool check_global_header(CheckContext *ctx, const AstNode *node,
                                const char *name)
{
    if (!check_global_name(ctx, node, name)) {
        return false;
    }
    if (node->left_child == AST_NO_NODE) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_NO_INITIALISER,
                        name);
        return false;
    }
    return true;
}

/* A top-level variable, a Store, an Atomic or a Channel. */
bool check_var_declaration(CheckContext *ctx, const AstNode *node,
                           size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    ResourceDeclaration resource = resource_declaration_for(ctx, node);
    DeclaredKind kind;
    TypedNode typed;
    size_t declared_type = TYPED_NO_NODE;
    size_t initialiser;
    size_t variable_type;

    if (!check_global_header(ctx, node, name)) {
        return false;
    }
    if (resource != NULL) {
        return resource(ctx, node, name, ast_index);
    }
    if (node->right_child != AST_NO_NODE &&
        !resolve_type_node(ctx, node->right_child, name, &declared_type)) {
        return false;
    }
    kind = declared_kind(ctx, node, name, declared_type);
    if (kind == DECLARED_STORE) {
        return check_store_declaration(ctx, node, name, ast_index, declared_type);
    }
    if (kind == DECLARED_REJECTED ||
        !check_initialiser(ctx, node, name, declared_type, &initialiser,
                           &variable_type, MESSAGE_CHECKER_STORE_INITIALIZER)) {
        return false;
    }
    typed = new_node(TYPED_DECLARATION, variable_type, node->pos, initialiser,
                     TYPED_NO_NODE);
    typed.name_index = checker_name(ctx, name);
    return emit_node(ctx, &typed, ast_index, &(size_t){0}) &&
           register_global(ctx, node, name, variable_type);
}


/* Names a program may not declare as a type. The first group each have their
   own message; the rest share one. */
static const struct {
    const char *name;
    const char *message;
} redefined_names[] = {
    {"Int32", MESSAGE_CHECKER_REDEFINES_INT32},
    {"Bool", MESSAGE_CHECKER_REDEFINES_BOOL},
    {"Nil", MESSAGE_CHECKER_REDEFINES_NIL},
    {"String", MESSAGE_CHECKER_REDEFINES_STRING},
    {"Text", MESSAGE_CHECKER_REDEFINES_TEXT},
    {"TextBuilder", MESSAGE_CHECKER_REDEFINES_BUILDER},
    {"List", MESSAGE_CHECKER_REDEFINES_LIST},
    {"Array", MESSAGE_CHECKER_REDEFINES_ARRAY},
    {"Slice", MESSAGE_CHECKER_REDEFINES_SLICE},
    {"Error", MESSAGE_CHECKER_REDEFINES_ERROR},
    {"Record", MESSAGE_CHECKER_REDEFINES_RECORD},
    {"Table", MESSAGE_CHECKER_REDEFINES_TABLE},
    {"Byte", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Store", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Handle", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Range", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Int8", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Int16", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Int64", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Uint8", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Uint16", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Uint32", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Uint64", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Float32", MESSAGE_CHECKER_REDEFINES_BUILTIN},
    {"Float64", MESSAGE_CHECKER_REDEFINES_BUILTIN},
};

/* The message for a name a program may not declare as a type, or NULL. */
static const char *redefinition_message(const char *name)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(redefined_names); i++) {
        if (strcmp(name, redefined_names[i].name) == 0) {
            return redefined_names[i].message;
        }
    }
    return NULL;
}

/* Why a program may not declare a type of this name, or NULL. */
static const char *type_name_problem(CheckContext *ctx, const char *name)
{
    const char *redefined = redefinition_message(name);

    if (redefined != NULL) {
        return redefined;
    }
    if (find_type_index(ctx->types, name) != TYPED_NO_NODE) {
        return MESSAGE_CHECKER_DUPLICATE_TYPE;
    }
    if (find_variable(ctx->globals, name) != NULL ||
        find_function(ctx, name) != NULL || name_is_resource(ctx, name)) {
        return MESSAGE_CHECKER_DUPLICATE_NAME;
    }
    return NULL;
}

static bool check_type_name(CheckContext *ctx, const AstNode *node,
                            const char *name)
{
    const char *problem;

    if (name == NULL) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_MISSING_NAME_OR_TYPE);
        return false;
    }
    if (!check_reserved_name(ctx, node->pos, name)) {
        return false;
    }
    problem = type_name_problem(ctx, name);
    if (problem != NULL) {
        error_report_at(ctx->errors, node->pos, problem, name);
    }
    return problem == NULL;
}

/* Record the type's name and type parameters. */
static bool declare_type(CheckContext *ctx, const AstNode *node,
                         const char *name, size_t ast_index, TypeEntry *entry)
{
    size_t i;

    if (node->type_parameters_count > NYX_MAX_TYPE_PARAMETERS) {
        error_report_fatal_at(ctx->errors, node->pos,
                              MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
        return false;
    }
    memcpy(entry->name, name, strlen(name) + 1);
    entry->kind = node->kind == AST_TYPE_ALIAS ? TYPE_ENTRY_ALIAS
                                               : TYPE_ENTRY_STRUCT;
    entry->ast_index = ast_index;
    entry->type_index = TYPED_NO_NODE;
    for (i = 0; i < node->type_parameters_count; i++) {
        if (!add_type_parameter_name(
                ctx,
                *(const size_t *)list_get(&ctx->ast->type_parameters,
                                          node->type_parameters_start + i),
                entry->parameters, &entry->parameter_count)) {
            return false;
        }
    }
    return checker_ok(ctx->errors, list_push(ctx->types, entry),
                      MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
}

static const AstNode *field_at(const CheckContext *ctx, const AstNode *node,
                               size_t i)
{
    return ast_get_node(ctx->ast,
                        *(const size_t *)list_get(&ctx->ast->children,
                                                  node->children_start + i));
}

/* Two fields of one name would be two members of one C struct. */
static bool check_duplicate_fields(CheckContext *ctx, const AstNode *node,
                                   const char *name)
{
    size_t first;
    size_t second;

    for (first = 0; first < node->children_count; first++) {
        const AstNode *field = field_at(ctx, node, first);
        const char *field_name =
            field == NULL ? NULL : ast_symbol_text(ctx->ast, field->name_index);

        for (second = first + 1; second < node->children_count; second++) {
            const AstNode *other = field_at(ctx, node, second);
            const char *other_name =
                other == NULL ? NULL
                              : ast_symbol_text(ctx->ast, other->name_index);

            if (field_name != NULL && other_name != NULL &&
                strcmp(field_name, other_name) == 0) {
                error_report_at(ctx->errors, other->pos,
                                MESSAGE_CHECKER_DUPLICATE_FIELD, field_name,
                                name);
                return false;
            }
        }
    }
    return true;
}

static bool check_expanding_fields(CheckContext *ctx, const AstNode *node,
                                   const char *name, const TypeEntry *entry)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        const AstNode *member = field_at(ctx, node, i);

        if (member != NULL && member->left_child != AST_NO_NODE &&
            self_use_expands(ctx, member->left_child, name, entry, 0)) {
            error_report_at(ctx->errors, member->pos,
                            MESSAGE_CHECKER_EXPANDING_RECURSION, name);
            return false;
        }
    }
    return true;
}

/* A non-generic alias is resolved at its declaration so unknown or invalid
   types are diagnosed even when the alias is never used. It has no C type and
   no root. Generic aliases expand per application. */
static bool resolve_declared_alias(CheckContext *ctx, TypeEntry *entry,
                                   size_t entry_index)
{
    size_t type_index;

    if (ctx->predeclaring_types || entry->parameter_count != 0) {
        return true;
    }
    if (!expand_alias(ctx, entry, NULL, 0, &type_index)) {
        return false;
    }
    ((TypeEntry *)list_get(ctx->types, entry_index))->type_index = type_index;
    return true;
}

static bool check_struct_fields(CheckContext *ctx, const AstNode *node,
                                const char *name, const TypeEntry *entry)
{
    if (node->children_count == 0) {
        error_report_at(ctx->errors, node->pos,
                        MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS);
        return false;
    }
    return check_duplicate_fields(ctx, node, name) &&
           (entry->parameter_count == 0 ||
            check_expanding_fields(ctx, node, name, entry));
}

bool check_type_declaration(CheckContext *ctx, const AstNode *node,
                            size_t ast_index)
{
    const char *name = ast_symbol_text(ctx->ast, node->name_index);
    TypeEntry entry = {0};
    size_t entry_index;
    size_t type_index;

    if (!check_type_name(ctx, node, name) ||
        !declare_type(ctx, node, name, ast_index, &entry)) {
        return false;
    }
    entry_index = ctx->types->length - 1;
    if (entry.kind == TYPE_ENTRY_ALIAS) {
        return resolve_declared_alias(ctx, &entry, entry_index);
    }
    if (!check_struct_fields(ctx, node, name, &entry)) {
        return false;
    }
    if (ctx->predeclaring_types || entry.parameter_count != 0) {
        return true;
    }
    if (!instantiate_struct(ctx, entry_index, NULL, 0, &type_index)) {
        return false;
    }
    ((TypeEntry *)list_get(ctx->types, entry_index))->type_index = type_index;
    return true;
}
