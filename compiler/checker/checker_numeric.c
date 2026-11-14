#include "checker_internal.h"

#include <float.h>
#include <limits.h>


/* 2^64 as a double: the first magnitude a uint64_t cannot hold. */
#define TWO_POW_64 18446744073709551616.0


NippyType unsigned_counterpart(NippyType kind)
{
    switch (kind) {
    case NIPPY_TYPE_INT8:
        return NIPPY_TYPE_BYTE;
    case NIPPY_TYPE_INT16:
        return NIPPY_TYPE_UINT16;
    case NIPPY_TYPE_INT32:
        return NIPPY_TYPE_UINT32;
    case NIPPY_TYPE_INT64:
        return NIPPY_TYPE_UINT64;
    default:
        return kind;
    }
}


bool integer_constant_fits(NippyType kind, bool negative, uint64_t magnitude)
{
    unsigned width = nippy_type_bit_width(kind);

    if (!nippy_type_is_integer(kind)) {
        return false;
    }
    if (nippy_type_is_signed_integer(kind)) {
        /* The negative range reaches one further than the positive range. */
        uint64_t limit = 1ULL << (width - 1);

        return negative ? magnitude <= limit : magnitude <= limit - 1;
    }
    if (negative && magnitude != 0) {
        return false;
    }
    return width == 64 ? true : magnitude <= (1ULL << width) - 1;
}


bool float_constant_fits(NippyType kind, double value)
{
    if (value != value) {
        return false;
    }
    if (kind == NIPPY_TYPE_FLOAT32) {
        return value <= FLT_MAX && value >= -FLT_MAX;
    }
    return kind == NIPPY_TYPE_FLOAT64 && value <= DBL_MAX && value >= -DBL_MAX;
}


bool numeric_constant_read(const TypedProgram *program, size_t node_index,
                           bool allow_converted, NumericConstant *out)
{
    const TypedNode *node = typed_program_node(program, node_index);

    memset(out, 0, sizeof(*out));
    if (node == NULL) {
        return false;
    }
    if (node->kind == TYPED_NUMERIC_CONVERSION) {
        /* A folded conversion points at its own target type. */
        if (!allow_converted || node->auxiliary_type != node->type_index) {
            return false;
        }
        return numeric_constant_read(program, node->left_child, true, out);
    }
    if (node->kind == TYPED_UNARY && node->op == NIPPY_OPERATOR_NEG) {
        NumericConstant operand;

        if (!numeric_constant_read(program, node->left_child, allow_converted,
                                   &operand)) {
            return false;
        }
        /* A doubled negation is not a literal; let the operator handle it. */
        if (operand.negative || (operand.is_float && operand.real < 0)) {
            return false;
        }
        *out = operand;
        if (operand.is_float) {
            out->real = -operand.real;
        } else {
            out->negative = true;
        }
        return true;
    }
    if (node->kind == TYPED_INTEGER_LITERAL) {
        out->magnitude = node->integer_value_u;
        return true;
    }
    if (node->kind == TYPED_FLOAT_LITERAL) {
        out->is_float = true;
        out->real = node->float_value;
        return true;
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
        !integer_constant_fits(NIPPY_TYPE_INT64, constant.negative,
                               constant.magnitude)) {
        error_report_at(ctx->errors, node->pos, MESSAGE_CHECKER_INTEGER_RANGE);
        return false;
    }
    return true;
}


bool numeric_constant_convert(const NumericConstant *in, NippyType target,
                              NumericConstant *out)
{
    memset(out, 0, sizeof(*out));
    if (nippy_type_is_float(target)) {
        double value = in->is_float
                           ? in->real
                           : (in->negative ? -(double)in->magnitude
                                           : (double)in->magnitude);

        if (!float_constant_fits(target, value)) {
            return false;
        }
        out->is_float = true;
        out->real = target == NIPPY_TYPE_FLOAT32 ? (double)(float)value : value;
        return true;
    }
    if (!nippy_type_is_integer(target)) {
        return false;
    }
    if (in->is_float) {
        double value = in->real;
        double absolute = value < 0 ? -value : value;

        /* The cast truncates toward zero; out-of-range magnitudes are
           rejected first because converting them would be undefined. */
        if (value != value || absolute >= TWO_POW_64) {
            return false;
        }
        out->magnitude = (uint64_t)absolute;
        out->negative = value < 0 && out->magnitude != 0;
    } else {
        out->magnitude = in->magnitude;
        out->negative = in->negative && in->magnitude != 0;
    }
    return integer_constant_fits(target, out->negative, out->magnitude);
}


static bool set_literal_type(CheckContext *ctx, TypedNode *node,
                             size_t type_index, NippyType kind)
{
    (void)ctx;
    node->type_index = type_index;
    if (node->kind == TYPED_FLOAT_LITERAL && kind == NIPPY_TYPE_FLOAT32) {
        /* Store the value the C float will really hold. */
        node->float_value = (double)(float)node->float_value;
    }
    return true;
}


bool numeric_constant_retype(CheckContext *ctx, size_t node_index,
                             size_t target_index, NippyType target_kind)
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
        if (nippy_type_is_integer(target_kind)) {
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
                           NippyType kind, size_t target_index, SourcePos pos,
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
    if (!typed_program_push(ctx->program, &literal, &literal_index)) {
        error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
        return false;
    }
    if (value->negative) {
        TypedNode negation = {0};

        negation.kind = TYPED_UNARY;
        negation.op = NIPPY_OPERATOR_NEG;
        negation.type_index = target_index;
        negation.pos = pos;
        negation.name_index = TYPED_NO_NODE;
        negation.left_child = literal_index;
        negation.right_child = TYPED_NO_NODE;
        negation.store_id = TYPED_NO_NODE;
        if (!typed_program_push(ctx->program, &negation, out_index)) {
            error_report_fatal(ctx->errors, MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
            return false;
        }
        return true;
    }
    *out_index = literal_index;
    return true;
}


bool numeric_constant_check_range(CheckContext *ctx, SourcePos pos,
                                  const NumericConstant *constant,
                                  NippyType kind)
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
    if (kind == NIPPY_TYPE_BYTE && constant->magnitude <= (uint64_t)INT_MAX) {
        int shown = (int)constant->magnitude;

        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_BYTE_RANGE,
                        constant->negative ? -shown : shown);
    } else {
        error_report_at(ctx->errors, pos, MESSAGE_CHECKER_INTEGER_RANGE);
    }
    return false;
}
