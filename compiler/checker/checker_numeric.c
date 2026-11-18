#include "checker_internal.h"

#include <float.h>
#include <limits.h>


/* 2^64 as a double: the first magnitude a uint64_t cannot hold. */
#define TWO_POW_64 18446744073709551616.0


NyxType unsigned_counterpart(NyxType kind)
{
    switch (kind) {
    case NYX_TYPE_INT8:
        return NYX_TYPE_BYTE;
    case NYX_TYPE_INT16:
        return NYX_TYPE_UINT16;
    case NYX_TYPE_INT32:
        return NYX_TYPE_UINT32;
    case NYX_TYPE_INT64:
        return NYX_TYPE_UINT64;
    default:
        return kind;
    }
}


bool integer_constant_fits(NyxType kind, bool negative, uint64_t magnitude)
{
    unsigned width = nyx_type_bit_width(kind);

    if (!nyx_type_is_integer(kind)) {
        return false;
    }
    if (nyx_type_is_signed_integer(kind)) {
        /* The negative range reaches one further than the positive range. */
        uint64_t limit = 1ULL << (width - 1);

        return negative ? magnitude <= limit : magnitude <= limit - 1;
    }
    if (negative && magnitude != 0) {
        return false;
    }
    return width == 64 ? true : magnitude <= (1ULL << width) - 1;
}


bool float_constant_fits(NyxType kind, double value)
{
    if (value != value) {
        return false;
    }
    if (kind == NYX_TYPE_FLOAT32) {
        return value <= FLT_MAX && value >= -FLT_MAX;
    }
    return kind == NYX_TYPE_FLOAT64 && value <= DBL_MAX && value >= -DBL_MAX;
}


/* A reader for one kind of typed node: whether the node is a constant, and
   its value. */
typedef bool (*ConstantReader)(const TypedProgram *program, const TypedNode *node,
                               bool allow_converted, NumericConstant *out);

bool numeric_constant_read(const TypedProgram *program, size_t node_index,
                           bool allow_converted, NumericConstant *out);

/* A folded conversion points at its own target type. */
static bool read_converted(const TypedProgram *program, const TypedNode *node,
                           bool allow_converted, NumericConstant *out)
{
    return allow_converted && node->auxiliary_type == node->type_index &&
           numeric_constant_read(program, node->left_child, true, out);
}

/* A negated literal. A doubled negation is not a literal; let the operator
   handle it. */
static bool read_negated(const TypedProgram *program, const TypedNode *node,
                         bool allow_converted, NumericConstant *out)
{
    NumericConstant operand;

    if (node->op != NYX_OPERATOR_NEG ||
        !numeric_constant_read(program, node->left_child, allow_converted,
                               &operand) ||
        operand.negative || (operand.is_float && operand.real < 0)) {
        return false;
    }
    *out = operand;
    out->real = operand.is_float ? -operand.real : operand.real;
    out->negative = !operand.is_float;
    return true;
}

static bool read_integer(const TypedProgram *program, const TypedNode *node,
                         bool allow_converted, NumericConstant *out)
{
    (void)program;
    (void)allow_converted;
    out->magnitude = node->integer_value_u;
    return true;
}

static bool read_float(const TypedProgram *program, const TypedNode *node,
                       bool allow_converted, NumericConstant *out)
{
    (void)program;
    (void)allow_converted;
    out->is_float = true;
    out->real = node->float_value;
    return true;
}

static const struct {
    TypedNodeKind kind;
    ConstantReader read;
} constant_readers[] = {
    {TYPED_NUMERIC_CONVERSION, read_converted},
    {TYPED_UNARY, read_negated},
    {TYPED_INTEGER_LITERAL, read_integer},
    {TYPED_FLOAT_LITERAL, read_float},
};

bool numeric_constant_read(const TypedProgram *program, size_t node_index,
                           bool allow_converted, NumericConstant *out)
{
    const TypedNode *node = typed_program_node(program, node_index);
    size_t i;

    memset(out, 0, sizeof(*out));
    for (i = 0; node != NULL && i < NYX_COUNT_OF(constant_readers); i++) {
        if (constant_readers[i].kind == node->kind) {
            return constant_readers[i].read(program, node, allow_converted, out);
        }
    }
    return false;
}

bool check_default_numeric(CheckContext *ctx, size_t value)
{
    NumericConstant constant;
    const TypedNode *node = typed_program_node(ctx->program, value);

    if (node != NULL && numeric_constant_read(ctx->program, value, false,
                                               &constant) &&
        !constant.is_float &&
        !integer_constant_fits(NYX_TYPE_INT64, constant.negative,
                               constant.magnitude)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_INTEGER_RANGE);
        return false;
    }
    return true;
}


/* A constant as a float of the given width. */
static bool convert_to_float(const NumericConstant *in, NyxType target,
                             NumericConstant *out)
{
    double value = in->is_float ? in->real
                                : (in->negative ? -(double)in->magnitude
                                                : (double)in->magnitude);

    if (!float_constant_fits(target, value)) {
        return false;
    }
    out->is_float = true;
    out->real = target == NYX_TYPE_FLOAT32 ? (double)(float)value : value;
    return true;
}

/* A constant as an integer of the given type. A float is truncated toward
   zero, once its magnitude is known to be in range, because converting a
   larger one would be undefined. */
static bool convert_to_integer(const NumericConstant *in, NyxType target,
                               NumericConstant *out)
{
    double value = in->real;
    double absolute = value < 0 ? -value : value;

    if (in->is_float && (value != value || absolute >= TWO_POW_64)) {
        return false;
    }
    out->magnitude = in->is_float ? (uint64_t)absolute : in->magnitude;
    out->negative = (in->is_float ? value < 0 : in->negative) &&
                    out->magnitude != 0;
    return integer_constant_fits(target, out->negative, out->magnitude);
}

bool numeric_constant_convert(const NumericConstant *in, NyxType target,
                              NumericConstant *out)
{
    memset(out, 0, sizeof(*out));
    if (nyx_type_is_float(target)) {
        return convert_to_float(in, target, out);
    }
    return nyx_type_is_integer(target) && convert_to_integer(in, target, out);
}


static bool set_literal_type(CheckContext *ctx, TypedNode *node,
                             size_t type_index, NyxType kind)
{
    (void)ctx;
    node->type_index = type_index;
    if (node->kind == TYPED_FLOAT_LITERAL && kind == NYX_TYPE_FLOAT32) {
        /* Store the value the C float will really hold. */
        node->float_value = (double)(float)node->float_value;
    }
    return true;
}


bool numeric_constant_retype(CheckContext *ctx, size_t node_index,
                             size_t target_index, NyxType target_kind)
{
    TypedNode *node = (TypedNode *)typed_program_node(ctx->program,
                                                      node_index);

    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_UNARY) {
        TypedNode *operand = (TypedNode *)typed_program_node(
            ctx->program, node->left_child);
        size_t operand_type = target_index;

        if (operand == NULL) {
            return false;
        }
        if (nyx_type_is_integer(target_kind)) {
            /* The magnitude of the most negative value does not fit the
               signed type, so the operand carries it unsigned. */
            if (!builtin_type(ctx, unsigned_counterpart(target_kind),
                              &operand_type)) {
                return false;
            }
        }
        node->type_index = target_index;
        return set_literal_type(ctx, operand, operand_type, target_kind);
    }
    return set_literal_type(ctx, node, target_index, target_kind);
}


bool numeric_constant_emit(CheckContext *ctx, const NumericConstant *value,
                           NyxType kind, size_t target_index, SourcePos pos,
                           size_t *out_index)
{
    TypedNode literal = {0};
    size_t literal_index;
    size_t literal_type = target_index;

    literal.pos = pos;
    literal.name_index = TYPED_NO_NODE;
    literal.left_child = TYPED_NO_NODE;
    literal.right_child = TYPED_NO_NODE;
    literal.store_id = TYPED_NO_NODE;
    if (value->is_float) {
        literal.kind = TYPED_FLOAT_LITERAL;
        literal.float_value = value->real;
    } else {
        literal.kind = TYPED_INTEGER_LITERAL;
        literal.integer_value_u = value->magnitude;
        literal.integer_value = value->magnitude <= (uint64_t)INT_MAX
                                    ? (int)value->magnitude
                                    : 0;
        if (value->negative &&
            !builtin_type(ctx, unsigned_counterpart(kind), &literal_type)) {
            return false;
        }
    }
    literal.type_index = literal_type;
    if (!checker_push_node(ctx, &literal, &literal_index, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
        return false;
    }
    if (value->negative) {
        TypedNode negation = {0};

        negation.kind = TYPED_UNARY;
        negation.op = NYX_OPERATOR_NEG;
        negation.type_index = target_index;
        negation.pos = pos;
        negation.name_index = TYPED_NO_NODE;
        negation.left_child = literal_index;
        negation.right_child = TYPED_NO_NODE;
        negation.store_id = TYPED_NO_NODE;
        if (!checker_push_node(ctx, &negation, out_index, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM)) {
            return false;
        }
        return true;
    }
    *out_index = literal_index;
    return true;
}


bool numeric_constant_check_range(CheckContext *ctx, SourcePos pos,
                                  const NumericConstant *constant,
                                  NyxType kind)
{
    if (constant->is_float) {
        if (float_constant_fits(kind, constant->real)) {
            return true;
        }
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_FLOAT_RANGE);
        return false;
    }
    if (integer_constant_fits(kind, constant->negative, constant->magnitude)) {
        return true;
    }
    /* The Byte message prints the value, which only works while it fits an
       int; a larger literal gets the generic range message. */
    if (kind == NYX_TYPE_BYTE && constant->magnitude <= (uint64_t)INT_MAX) {
        int shown = (int)constant->magnitude;

        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_BYTE_RANGE,
                        constant->negative ? -shown : shown);
    } else {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_INTEGER_RANGE);
    }
    return false;
}
