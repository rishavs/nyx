#include "generator_internal.h"

/* Operators, calls, and Store allocation as C expressions. A kind with several
   shapes (a binary operation, a numeric conversion, a unary operation) is an
   ordered list of rules: each looks at the node and either writes its C and
   says so, or passes. The first rule to take the node wins, so the order is
   the precedence. */

typedef enum {
    EMIT_NEXT,   /* not this rule's shape; try the next */
    EMIT_DONE,   /* written */
    EMIT_FAILED  /* the error is reported */
} Emit;

static Emit emit_result(bool ok)
{
    return ok ? EMIT_DONE : EMIT_FAILED;
}

static Emit emit_unsupported(ErrorState *errors)
{
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return EMIT_FAILED;
}

static const char *const unsigned_names[NYX_TYPE_KIND_COUNT] = {
    [NYX_TYPE_INT8] = "uint8_t",    [NYX_TYPE_BYTE] = "uint8_t",
    [NYX_TYPE_INT16] = "uint16_t",  [NYX_TYPE_UINT16] = "uint16_t",
    [NYX_TYPE_INT32] = "uint32_t",  [NYX_TYPE_UINT32] = "uint32_t",
    [NYX_TYPE_INT64] = "uint64_t",  [NYX_TYPE_UINT64] = "uint64_t",
};

/* Arithmetic is done in at least 32 bits. Narrower unsigned operands are
   promoted to signed int by C, so 65535 * 65535 in uint16_t would overflow a
   signed int; computing in uint32_t keeps every product defined. */
static const char *compute_type_name(NyxType kind)
{
    return nyx_type_bit_width(kind) < 32 ? "uint32_t" : unsigned_names[kind];
}

/* The suffix of the runtime helpers for each integer type. */
static const char *const arith_suffixes[NYX_TYPE_KIND_COUNT] = {
    [NYX_TYPE_INT8] = "i8",    [NYX_TYPE_INT16] = "i16",
    [NYX_TYPE_INT32] = "i32",  [NYX_TYPE_INT64] = "i64",
    [NYX_TYPE_BYTE] = "u8",    [NYX_TYPE_UINT16] = "u16",
    [NYX_TYPE_UINT32] = "u32", [NYX_TYPE_UINT64] = "u64",
};

/* A conversion between integer types needs a runtime check unless every value
   of the source is representable in the target. */
static bool integer_conversion_always_fits(NyxType source, NyxType target)
{
    bool source_signed = nyx_type_is_signed_integer(source);
    unsigned source_width = nyx_type_bit_width(source);
    unsigned target_width = nyx_type_bit_width(target);

    if (source_signed == nyx_type_is_signed_integer(target)) {
        return target_width >= source_width;
    }
    return !source_signed && target_width > source_width;
}

/* ---- numeric conversion ---- */

typedef struct {
    const TypedProgram *program;
    const TypedNode *node;
    TextBuffer *buffer;
    ErrorState *errors;
    NyxType source;
    NyxType target;
    char type_name[NYX_IDENTIFIER_MAX + 32];
} Conversion;

typedef Emit (*ConversionRule)(Conversion *c);

/* Same type, or a constant already converted by the checker. */
static Emit conversion_identity(Conversion *c)
{
    if (c->target != c->source) {
        return EMIT_NEXT;
    }
    return emit_result(emit_expression(c->program, c->node->left_child,
                                       c->buffer, c->errors));
}

static Emit conversion_float_to_integer(Conversion *c)
{
    char head[64];
    char tail[96];

    if (!nyx_type_is_float(c->source) || !nyx_type_is_integer(c->target)) {
        return EMIT_NEXT;
    }
    snprintf(head, sizeof(head), "nyx_f2i_%s((double)(",
             arith_suffixes[c->target]);
    snprintf(tail, sizeof(tail), "), __FILE__, %zu, %zu, %zu)",
             c->node->pos.line, c->node->pos.column, c->node->pos.offset);
    return emit_result(emit_operand(c->program, c->node, c->buffer, c->errors,
                                    head, tail));
}

static Emit conversion_integer_checked(Conversion *c)
{
    bool from_signed = nyx_type_is_signed_integer(c->source);
    char head[96];
    char tail[96];

    if (!nyx_type_is_integer(c->source) || !nyx_type_is_integer(c->target) ||
        integer_conversion_always_fits(c->source, c->target)) {
        return EMIT_NEXT;
    }
    snprintf(head, sizeof(head), "nyx_c%c_%s((%s)(", from_signed ? 's' : 'u',
             arith_suffixes[c->target], from_signed ? "int64_t" : "uint64_t");
    snprintf(tail, sizeof(tail), "), __FILE__, %zu, %zu, %zu)",
             c->node->pos.line, c->node->pos.column, c->node->pos.offset);
    return emit_result(emit_operand(c->program, c->node, c->buffer, c->errors,
                                    head, tail));
}

static Emit conversion_float_narrowing(Conversion *c)
{
    char tail[96];

    if (c->target != NYX_TYPE_FLOAT32 || c->source != NYX_TYPE_FLOAT64) {
        return EMIT_NEXT;
    }
    snprintf(tail, sizeof(tail), ", __FILE__, %zu, %zu, %zu)",
             c->node->pos.line, c->node->pos.column, c->node->pos.offset);
    return emit_result(emit_operand(c->program, c->node, c->buffer, c->errors,
                                    "nyx_f2f_f32(", tail));
}

/* Widening integer, integer to float, and float widening cannot fail, so a
   plain C conversion is exact or correctly rounded. */
static Emit conversion_plain(Conversion *c)
{
    char head[NYX_IDENTIFIER_MAX + 48];

    snprintf(head, sizeof(head), "((%s)(", c->type_name);
    return emit_result(emit_operand(c->program, c->node, c->buffer, c->errors,
                                    head, "))"));
}

static const ConversionRule conversion_rules[] = {
    conversion_identity,
    conversion_float_to_integer,
    conversion_integer_checked,
    conversion_float_narrowing,
    conversion_plain,
};

static bool emit_operator_numeric_conversion(const TypedProgram *program,
                                             const TypedNode *node,
                                             TextBuffer *buffer,
                                             ErrorState *errors)
{
    const TypedType *target = typed_program_type(program, node->type_index);
    const TypedType *source = typed_program_type(program, node->auxiliary_type);
    Conversion c = {program, node, buffer, errors, source->kind, target->kind,
                    ""};
    size_t i;
    Emit result = EMIT_NEXT;

    if (!format_c_type(program, node->type_index, c.type_name,
                       sizeof(c.type_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    for (i = 0; result == EMIT_NEXT && i < NYX_COUNT_OF(conversion_rules); i++) {
        result = conversion_rules[i](&c);
    }
    return result == EMIT_DONE;
}

/* ---- binary operations ---- */

typedef struct {
    const TypedProgram *program;
    const TypedNode *node;
    TextBuffer *buffer;
    ErrorState *errors;
    const TypedNode *left;
    const TypedNode *right;
    const TypedType *left_type;
    const TypedType *right_type;
    char type_name[NYX_IDENTIFIER_MAX + 32];
} Binary;

typedef Emit (*BinaryRule)(Binary *c);

static bool is_equality(NyxOperator op)
{
    return op == NYX_OPERATOR_EQ || op == NYX_OPERATOR_NE ||
           op == NYX_OPERATOR_STRICT_EQ;
}

static bool is_string_kind(NyxType kind)
{
    return kind == NYX_TYPE_STRING || kind == NYX_TYPE_DYNAMIC_STRING;
}

static bool is_handle_type(const TypedType *type)
{
    return type->kind == NYX_TYPE_HANDLE;
}

/* `(`, or `(!` for a not-equal. */
static const char *negation(const TypedNode *node)
{
    return node->op == NYX_OPERATOR_NE ? "(!" : "(";
}

static bool emit_binary_operands(Binary *c, const char *open,
                                 const char *middle, const char *close)
{
    return emit_operands(c->program, c->node, c->buffer, c->errors, open,
                         middle, close);
}

/* Values of different types compared strictly are never equal, but each side
   is still evaluated, and a Handle on either side is still validated. */
static Emit binary_strict_mismatch(Binary *c)
{
    char left_tail[96] = "";
    char middle[160];
    char right_tail[96] = "";
    char close[128];

    if (c->node->op != NYX_OPERATOR_STRICT_EQ ||
        typed_types_equivalent(c->program, c->left->type_index,
                               c->right->type_index, 64)) {
        return EMIT_NEXT;
    }
    if (is_handle_type(c->left_type)) {
        position_text(c->node, left_tail, sizeof(left_tail));
    }
    if (is_handle_type(c->right_type)) {
        position_text(c->node, right_tail, sizeof(right_tail));
    }
    snprintf(middle, sizeof(middle), "%s%s",
             left_tail, is_handle_type(c->right_type)
                            ? "), nyx_equal_validate_handle("
                            : "), (void)(");
    snprintf(close, sizeof(close), "%s), false)", right_tail);
    return emit_result(emit_binary_operands(
        c, is_handle_type(c->left_type) ? "(nyx_equal_validate_handle("
                                        : "((void)(",
        middle, close));
}

static bool is_composite_kind(NyxType kind)
{
    return kind == NYX_TYPE_STRUCT || kind == NYX_TYPE_FIXED_LIST ||
           kind == NYX_TYPE_LIST || kind == NYX_TYPE_RECORD ||
           kind == NYX_TYPE_TABLE || kind == NYX_TYPE_HANDLE;
}

/* Two string types that would not be the same C type. */
static bool strings_differ(const Binary *c)
{
    return is_string_kind(c->left_type->kind) &&
           is_string_kind(c->right_type->kind) &&
           !typed_types_equivalent(c->program, c->left->type_index,
                                   c->right->type_index, 64);
}

/* Two values compared with a generated equality helper: a composite type, or
   two different string types. */
static Emit binary_helper_equality(Binary *c)
{
    char open[96];
    char close[96];

    if (!is_equality(c->node->op) ||
        !(is_composite_kind(c->left_type->kind) || strings_differ(c))) {
        return EMIT_NEXT;
    }
    if (is_string_kind(c->left_type->kind)) {
        snprintf(open, sizeof(open), "%snyx_equal_pair_%zu_%zu(",
                 negation(c->node), c->left->type_index, c->right->type_index);
    } else {
        snprintf(open, sizeof(open), "%snyx_equal_%zu(", negation(c->node),
                 c->left->type_index);
    }
    position_text(c->node, close, sizeof(close));
    strcat(close, "))");
    return emit_result(emit_binary_operands(c, open, ", ", close));
}

/* The helper that compares two numeric domains exactly. */
static const char *mixed_equality_helper(NyxType left, NyxType right)
{
    static const struct {
        bool (*first)(NyxType);
        bool (*second)(NyxType);
        const char *forward;  /* first on the left */
        const char *backward; /* second on the left */
    } rows[] = {
        {nyx_type_is_signed_integer, nyx_type_is_unsigned_integer,
         "nyx_eq_su", "nyx_eq_us"},
        {nyx_type_is_signed_integer, nyx_type_is_float, "nyx_eq_sf",
         "nyx_eq_fs"},
        {nyx_type_is_unsigned_integer, nyx_type_is_float, "nyx_eq_uf",
         "nyx_eq_fu"},
    };
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(rows); i++) {
        if (rows[i].first(left) && rows[i].second(right)) {
            return rows[i].forward;
        }
        if (rows[i].second(left) && rows[i].first(right)) {
            return rows[i].backward;
        }
    }
    return NULL;
}

static Emit binary_mixed_numeric_equality(Binary *c)
{
    const char *helper;
    char open[64];

    if ((c->node->op != NYX_OPERATOR_EQ && c->node->op != NYX_OPERATOR_NE) ||
        !nyx_type_is_numeric(c->left_type->kind) ||
        !nyx_type_is_numeric(c->right_type->kind)) {
        return EMIT_NEXT;
    }
    helper = mixed_equality_helper(c->left_type->kind, c->right_type->kind);
    if (helper == NULL) {
        return EMIT_NEXT;
    }
    snprintf(open, sizeof(open), "%s%s(", negation(c->node), helper);
    return emit_result(emit_binary_operands(c, open, ", ", "))"));
}

static Emit binary_dynamic_string_equality(Binary *c)
{
    char open[64];
    char close[128];

    if (!is_equality(c->node->op) ||
        c->left_type->kind != NYX_TYPE_DYNAMIC_STRING) {
        return EMIT_NEXT;
    }
    snprintf(open, sizeof(open), "%snyx_string_equal_at(", negation(c->node));
    position_text(c->node, close, sizeof(close));
    strcat(close, "))");
    return emit_result(emit_binary_operands(c, open, ", ", close));
}

static Emit binary_fixed_string_equality(Binary *c)
{
    char open[64];

    if (!is_equality(c->node->op) || c->left_type->kind != NYX_TYPE_STRING) {
        return EMIT_NEXT;
    }
    snprintf(open, sizeof(open), "%snyx_string_%zu_equal(", negation(c->node),
             c->left_type->fixed_length);
    return emit_result(emit_binary_operands(c, open, ", ", "))"));
}

/* The C type of the result, which the arithmetic rules below spell out. */
static Emit binary_result_type(Binary *c)
{
    return format_c_type(c->program, c->node->type_index, c->type_name,
                         sizeof(c->type_name), c->errors)
               ? EMIT_NEXT
               : EMIT_FAILED;
}

/* +, - and * are done in the unsigned type of the operand's width, which wraps
   instead of overflowing, and converted back. */
static Emit binary_wrapping_arithmetic(Binary *c)
{
    NyxOperator op = c->node->op;
    const char *unsigned_type = compute_type_name(c->left_type->kind);
    char open[NYX_IDENTIFIER_MAX + 64];
    char middle[64];
    char close[16];

    if (!nyx_type_is_integer(c->left_type->kind) ||
        (op != NYX_OPERATOR_ADD && op != NYX_OPERATOR_SUB &&
         op != NYX_OPERATOR_MUL)) {
        return EMIT_NEXT;
    }
    snprintf(open, sizeof(open), "(%s)((%s)(", c->type_name, unsigned_type);
    snprintf(middle, sizeof(middle), ") %s (%s)(", binary_operator_text(op),
             unsigned_type);
    snprintf(close, sizeof(close), "))");
    return emit_result(emit_binary_operands(c, open, middle, close));
}

/* Division, remainder and shifts go through checked helpers. */
static Emit binary_checked_arithmetic(Binary *c)
{
    static const struct {
        NyxOperator op;
        const char *prefix;
        bool positioned; /* the helper reports a failure at the source */
    } helpers[] = {
        {NYX_OPERATOR_DIV, "nyx_div_", true},
        {NYX_OPERATOR_MOD, "nyx_mod_", true},
        {NYX_OPERATOR_SHL, "nyx_shl_", false},
        {NYX_OPERATOR_SHR, "nyx_shr_", false},
    };
    size_t i;

    for (i = 0; nyx_type_is_integer(c->left_type->kind) &&
                i < NYX_COUNT_OF(helpers);
         i++) {
        char open[64];
        char close[112] = ")";

        if (helpers[i].op != c->node->op) {
            continue;
        }
        snprintf(open, sizeof(open), "%s%s(", helpers[i].prefix,
                 arith_suffixes[c->left_type->kind]);
        if (helpers[i].positioned) {
            position_text(c->node, close, sizeof(close));
            strcat(close, ")");
        }
        return emit_result(emit_binary_operands(c, open, ", ", close));
    }
    return EMIT_NEXT;
}

/* Everything else is the C operator with the same meaning. */
static Emit binary_infix(Binary *c)
{
    const char *op = binary_operator_text(c->node->op);
    char middle[16];

    if (op == NULL) {
        return emit_unsupported(c->errors);
    }
    snprintf(middle, sizeof(middle), " %s ", op);
    return emit_result(emit_binary_operands(c, "(", middle, ")"));
}

static const BinaryRule binary_rules[] = {
    binary_strict_mismatch,
    binary_helper_equality,
    binary_mixed_numeric_equality,
    binary_dynamic_string_equality,
    binary_fixed_string_equality,
    binary_result_type,
    binary_wrapping_arithmetic,
    binary_checked_arithmetic,
    binary_infix,
};

static bool emit_operator_binary(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *buffer,
                                 ErrorState *errors)
{
    const TypedNode *left = typed_program_node(program, node->left_child);
    const TypedNode *right = typed_program_node(program, node->right_child);
    Binary c = {program, node, buffer, errors, left, right,
                typed_program_type(program, left->type_index),
                typed_program_type(program, right->type_index), ""};
    size_t i;
    Emit result = EMIT_NEXT;

    for (i = 0; result == EMIT_NEXT && i < NYX_COUNT_OF(binary_rules); i++) {
        result = binary_rules[i](&c);
    }
    return result == EMIT_DONE;
}

/* ---- unary operations ---- */

static bool emit_unary_not(const TypedProgram *program, const TypedNode *node,
                           TextBuffer *buffer, ErrorState *errors)
{
    return emit_operand(program, node, buffer, errors, "(!(", "))");
}

static bool emit_unary_negation(const TypedProgram *program,
                                const TypedNode *node, TextBuffer *buffer,
                                ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    const char *unsigned_type = type == NULL ? NULL
                                             : compute_type_name(type->kind);
    char type_name[NYX_IDENTIFIER_MAX + 32];
    char open[NYX_IDENTIFIER_MAX + 96];

    if (!format_c_type(program, node->type_index, type_name, sizeof(type_name),
                       errors)) {
        return false;
    }
    if (type != NULL && nyx_type_is_float(type->kind)) {
        return emit_operand(program, node, buffer, errors, "(-(", "))");
    }
    if (unsigned_type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        return false;
    }
    snprintf(open, sizeof(open), "(%s)((%s)0 - (%s)(", type_name, unsigned_type,
             unsigned_type);
    return emit_operand(program, node, buffer, errors, open, "))");
}

/* C promotes a narrow operand to int, so ~ on a Uint8 would be negative; the
   cast brings it back to the operand's own type. */
static bool emit_unary_complement(const TypedProgram *program,
                                  const TypedNode *node, TextBuffer *buffer,
                                  ErrorState *errors)
{
    char type_name[NYX_IDENTIFIER_MAX + 32];
    char open[NYX_IDENTIFIER_MAX + 48];

    if (!format_c_type(program, node->type_index, type_name, sizeof(type_name),
                       errors)) {
        return false;
    }
    snprintf(open, sizeof(open), "((%s)(~(", type_name);
    return emit_operand(program, node, buffer, errors, open, ")))");
}

static const struct {
    NyxOperator op;
    bool (*emit)(const TypedProgram *, const TypedNode *, TextBuffer *,
                 ErrorState *);
} unary_emitters[] = {
    {NYX_OPERATOR_NOT, emit_unary_not},
    {NYX_OPERATOR_NEG, emit_unary_negation},
    {NYX_OPERATOR_BIT_NOT, emit_unary_complement},
};

static bool emit_operator_unary(const TypedProgram *program,
                                const TypedNode *node, TextBuffer *buffer,
                                ErrorState *errors)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(unary_emitters); i++) {
        if (unary_emitters[i].op == node->op) {
            return unary_emitters[i].emit(program, node, buffer, errors);
        }
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}

/* ---- constructors and calls ---- */

static bool emit_operator_struct_constructor(const TypedProgram *program,
                                             const TypedNode *node,
                                             TextBuffer *buffer,
                                             ErrorState *errors)
{
    char type_name[NYX_IDENTIFIER_MAX + 32];
    bool ok = format_c_type(program, node->type_index, type_name,
                            sizeof(type_name), errors);

    if (!ok) {
        return false;
    }
    buffer_appendf(buffer, "(%s){ ", type_name);
    ok = emit_children_expressions(program, node, buffer, errors);
    buffer_append(buffer, " }");
    return ok;
}

/* A method call passes its receiver by address. */
static bool emit_call_arguments(const TypedProgram *program,
                                const TypedNode *node, TextBuffer *buffer,
                                ErrorState *errors)
{
    size_t i;
    bool ok = true;

    if (!node->is_method_call) {
        return emit_children_expressions(program, node, buffer, errors);
    }
    for (i = 0; ok && i < node->children_count; i++) {
        buffer_append(buffer, i == 0 ? "&(" : ", ");
        ok = emit_expression(program,
                             *(const size_t *)list_get(
                                 &program->children, node->children_start + i),
                             buffer, errors);
        if (i == 0) {
            buffer_append(buffer, ")");
        }
    }
    return ok;
}

static bool emit_operator_function_call(const TypedProgram *program,
                                        const TypedNode *node,
                                        TextBuffer *buffer, ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = checked_user_name(program, node->name_index, name_text,
                                         sizeof(name_text), errors);
    bool ok;

    if (name == NULL) {
        return false;
    }
    buffer_appendf(buffer, "%s(", name);
    ok = emit_call_arguments(program, node, buffer, errors);
    buffer_append(buffer, ")");
    return ok;
}

/* The C literal that holds one element of a Store: an aggregate goes through a
   union so a braced initialiser reads as one value. */
static void emit_element_literal_open(TextBuffer *buffer, bool aggregate,
                                      const char *element_name)
{
    if (aggregate) {
        buffer_appendf(buffer, "&(union { %s nyx_value; }){ .nyx_value = ",
                       element_name);
    } else {
        buffer_appendf(buffer, "&(%s){ ", element_name);
    }
}

static bool emit_operator_store_alloc(const TypedProgram *program,
                                      const TypedNode *node, TextBuffer *buffer,
                                      ErrorState *errors)
{
    size_t element;
    char element_name[NYX_IDENTIFIER_MAX + 32];
    bool ok;

    if (!store_reference_is_valid(program, node) ||
        !handle_element_type(program, node->type_index, &element) ||
        node->left_child == TYPED_NO_NODE) {
        return emit_unsupported(errors) == EMIT_DONE;
    }
    if (!format_c_type(program, element, element_name, sizeof(element_name),
                       errors)) {
        return false;
    }
    buffer_append(buffer, "nyx_store_alloc_at(");
    ok = emit_store_reference(program, node, buffer, errors);
    buffer_append(buffer, ", ");
    emit_element_literal_open(buffer, type_is_c_aggregate(program, element),
                              element_name);
    ok = emit_expression(program, node->left_child, buffer, errors) && ok;
    buffer_appendf(buffer,
                   " }, sizeof(%s), _Alignof(%s), __FILE__, %zu, %zu, %zu)",
                   element_name, element_name, node->pos.line, node->pos.column,
                   node->pos.offset);
    return ok;
}

static bool emit_operator_handle_read(const TypedProgram *program,
                                      const TypedNode *node, TextBuffer *buffer,
                                      ErrorState *errors)
{
    char type_name[NYX_IDENTIFIER_MAX + 32];
    bool ok;

    if (node->left_child == TYPED_NO_NODE) {
        return emit_unsupported(errors) == EMIT_DONE;
    }
    if (!format_c_type(program, node->type_index, type_name, sizeof(type_name),
                       errors)) {
        return false;
    }
    buffer_appendf(buffer, "(*(%s *)nyx_handle_pointer_at(", type_name);
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_appendf(buffer, ", sizeof(%s), __FILE__, %zu, %zu, %zu))", type_name,
                   node->pos.line, node->pos.column, node->pos.offset);
    return ok;
}

static bool emit_operator_handle_write(const TypedProgram *program,
                                       const TypedNode *node,
                                       TextBuffer *buffer, ErrorState *errors)
{
    const TypedNode *handle = typed_program_node(program, node->left_child);
    const TypedType *result = typed_program_type(program, node->type_index);
    size_t element;
    char element_name[NYX_IDENTIFIER_MAX + 32];
    bool ok;

    if (handle == NULL || result == NULL || result->kind != NYX_TYPE_NIL ||
        node->right_child == TYPED_NO_NODE ||
        !handle_element_type(program, handle->type_index, &element)) {
        return emit_unsupported(errors) == EMIT_DONE;
    }
    if (!format_c_type(program, element, element_name, sizeof(element_name),
                       errors)) {
        return false;
    }
    buffer_append(buffer, "((void)nyx_handle_write_at(");
    ok = emit_expression(program, node->left_child, buffer, errors);
    buffer_append(buffer, ", ");
    emit_element_literal_open(buffer, type_is_c_aggregate(program, element),
                              element_name);
    ok = emit_expression(program, node->right_child, buffer, errors) && ok;
    buffer_appendf(buffer,
                   " }, sizeof(%s), __FILE__, %zu, %zu, %zu), (nyx_nil)0)",
                   element_name, node->pos.line, node->pos.column,
                   node->pos.offset);
    return ok;
}

typedef bool (*ExprOperatorEmitter)(const TypedProgram *, const TypedNode *,
                                    TextBuffer *, ErrorState *);

static const struct {
    TypedNodeKind kind;
    ExprOperatorEmitter emit;
} operator_emitters[] = {
    {TYPED_NUMERIC_CONVERSION, emit_operator_numeric_conversion},
    {TYPED_BINARY, emit_operator_binary},
    {TYPED_UNARY, emit_operator_unary},
    {TYPED_STRUCT_CONSTRUCTOR, emit_operator_struct_constructor},
    {TYPED_FUNCTION_CALL, emit_operator_function_call},
    {TYPED_STORE_ALLOC, emit_operator_store_alloc},
    {TYPED_HANDLE_READ, emit_operator_handle_read},
    {TYPED_HANDLE_WRITE, emit_operator_handle_write},
};

bool emit_expr_operators(const TypedProgram *program, const TypedNode *node,
                         TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    size_t i;

    for (i = 0; i < NYX_COUNT_OF(operator_emitters); i++) {
        if (operator_emitters[i].kind == node->kind) {
            *handled = true;
            return operator_emitters[i].emit(program, node, buffer, errors);
        }
    }
    return true;
}
