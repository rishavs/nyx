#include "generator_internal.h"

static bool emit_call_arguments(const TypedProgram *program,
                                const TypedNode *node, TextBuffer *buffer,
                                ErrorState *errors)
{
    size_t i;

    if (!node->is_method_call) {
        return emit_children_expressions(program, node, buffer, errors);
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &program->children, node->children_start + i);

        if (i > 0 && !buffer_append(buffer, ", ")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (i == 0 && !buffer_append(buffer, "&(")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!emit_expression(program, child, buffer, errors)) {
            return false;
        }
        if (i == 0 && !buffer_append(buffer, ")")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}

static const char *unsigned_type_name(NippyType kind)
{
    switch (kind) {
    case NIPPY_TYPE_INT8:
    case NIPPY_TYPE_BYTE:
        return "uint8_t";
    case NIPPY_TYPE_INT16:
    case NIPPY_TYPE_UINT16:
        return "uint16_t";
    case NIPPY_TYPE_INT32:
    case NIPPY_TYPE_UINT32:
        return "uint32_t";
    case NIPPY_TYPE_INT64:
    case NIPPY_TYPE_UINT64:
        return "uint64_t";
    default:
        return NULL;
    }
}


/* Arithmetic is done in at least 32 bits. Narrower unsigned operands are
   promoted to signed int by C, so 65535 * 65535 in uint16_t would overflow a
   signed int; computing in uint32_t keeps every product defined. */
static const char *compute_type_name(NippyType kind)
{
    return nippy_type_bit_width(kind) < 32 ? "uint32_t"
                                           : unsigned_type_name(kind);
}


/* A conversion between integer types needs a runtime check unless every value
   of the source is representable in the target. */
static bool integer_conversion_always_fits(NippyType source, NippyType target)
{
    bool source_signed = nippy_type_is_signed_integer(source);
    bool target_signed = nippy_type_is_signed_integer(target);
    unsigned source_width = nippy_type_bit_width(source);
    unsigned target_width = nippy_type_bit_width(target);

    if (source_signed == target_signed) {
        return target_width >= source_width;
    }
    return !source_signed && target_width > source_width;
}


static const char *arith_suffix(NippyType kind)
{
    switch (kind) {
    case NIPPY_TYPE_INT8:
        return "i8";
    case NIPPY_TYPE_INT16:
        return "i16";
    case NIPPY_TYPE_INT32:
        return "i32";
    case NIPPY_TYPE_INT64:
        return "i64";
    case NIPPY_TYPE_BYTE:
        return "u8";
    case NIPPY_TYPE_UINT16:
        return "u16";
    case NIPPY_TYPE_UINT32:
        return "u32";
    case NIPPY_TYPE_UINT64:
        return "u64";
    default:
        return NULL;
    }
}



typedef bool (*EmitExprOperatorsHandler)(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled);

static bool emit_operator_numeric_conversion_target(const TypedProgram *program, const TypedNode *node, char (*type_name)[64], ErrorState *errors, TextBuffer *buffer, const TypedType **source, const TypedType **target, const char **suffix, bool *outcome)
{
    (*target) = typed_program_type(program,
                                                 node->type_index);
    (*source) = typed_program_type(program,
                                                 node->auxiliary_type);
    

    if ((*target) == NULL || (*source) == NULL ||
        !format_c_type(program, node->type_index, (*type_name),
                       sizeof((*type_name)), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        do { *outcome = (false); return true; } while (0);
    }
    if ((*target)->kind == (*source)->kind) {
        /* Same type, or a constant already converted by the checker. */
        do { *outcome = (emit_expression(program, node->left_child, buffer,
                               errors)); return true; } while (0);
    }
    if (nippy_type_is_float((*source)->kind) &&
        nippy_type_is_integer((*target)->kind)) {
        (*suffix) = arith_suffix((*target)->kind);
        if ((*suffix) == NULL ||
            !buffer_appendf(buffer, "nyx_f2i_%s((double)(", (*suffix)) ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_appendf(buffer, "), __FILE__, %zu, %zu, %zu)",
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    return false;
}

static bool emit_operator_numeric_conversion_from_signed(const TypedType *source, const TypedType *target, const char **suffix, TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, bool *outcome2)
{
    if (nippy_type_is_integer(source->kind) &&
        nippy_type_is_integer(target->kind) &&
        !integer_conversion_always_fits(source->kind, target->kind)) {
        bool from_signed = nippy_type_is_signed_integer(source->kind);

        (*suffix) = arith_suffix(target->kind);
        if ((*suffix) == NULL ||
            !buffer_appendf(buffer, "nyx_c%c_%s((%s)(",
                            from_signed ? 's' : 'u', (*suffix),
                            from_signed ? "int64_t" : "uint64_t") ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_appendf(buffer, "), __FILE__, %zu, %zu, %zu)",
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            do { *outcome2 = (false); return true; } while (0);
        }
        do { *outcome2 = (true); return true; } while (0);
    }
    return false;
}

static bool emit_operator_numeric_conversion(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    {
        const TypedType *source = {0};
        const TypedType *target = {0};
        const char *suffix = {0};
        {
        bool outcome;

        if (emit_operator_numeric_conversion_target(program, node, &type_name, errors, buffer, &source, &target, &suffix, &outcome)) {
            return outcome;
        }
    }
        {
        bool outcome2;

        if (emit_operator_numeric_conversion_from_signed(source, target, &suffix, buffer, program, node, errors, &outcome2)) {
            return outcome2;
        }
    }
        if (target->kind == NIPPY_TYPE_FLOAT32 &&
            source->kind == NIPPY_TYPE_FLOAT64) {
            if (!buffer_append(buffer, "nyx_f2f_f32(") ||
                !emit_expression(program, node->left_child, buffer,
                                 errors) ||
                !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu)",
                                node->pos.line, node->pos.column,
                                node->pos.offset)) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        /* Widening integer, integer to float, and float widening cannot
           fail, so a plain C conversion is exact or correctly rounded. */
        if (!buffer_appendf(buffer, "((%s)(", type_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
}

static bool emit_operator_binary_buffer_append(TextBuffer *buffer, const TypedType *left_type, const TypedProgram *program, const TypedNode *node, ErrorState *errors, const TypedType *right_type)
{
    if (!buffer_append(buffer, left_type->kind == NIPPY_TYPE_HANDLE
                                   ? "(nyx_equal_validate_handle("
                                   : "((void)(") ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        (left_type->kind == NIPPY_TYPE_HANDLE &&
         !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu",
                         node->pos.line, node->pos.column,
                         node->pos.offset)) ||
        !buffer_append(buffer, right_type != NULL &&
                                  right_type->kind == NIPPY_TYPE_HANDLE
                                  ? "), nyx_equal_validate_handle("
                                  : "), (void)(") ||
        !emit_expression(program, node->right_child, buffer, errors) ||
        (right_type != NULL && right_type->kind == NIPPY_TYPE_HANDLE &&
         !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu",
                         node->pos.line, node->pos.column,
                         node->pos.offset)) ||
        !buffer_append(buffer, "), false)")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_binary_buffer_append4(const TypedNode *node, const TypedType *left_type, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, char (*type_name)[64], bool *outcome)
{
    if ((node->op == NIPPY_OPERATOR_EQ ||
         node->op == NIPPY_OPERATOR_STRICT_EQ ||
         node->op == NIPPY_OPERATOR_NE) &&
        left_type->kind == NIPPY_TYPE_STRING) {
        if (!buffer_append(buffer,
                           node->op == NIPPY_OPERATOR_NE ? "(!"
                                                         : "(") ||
            !buffer_appendf(buffer, "nyx_string_%zu_equal(",
                            left_type->fixed_length) ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer,
                             errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    if (!format_c_type(program, node->type_index, (*type_name),
                       sizeof((*type_name)), errors)) {
        do { *outcome = (false); return true; } while (0);
    }
    return false;
}

static bool emit_operator_binary_buffer_append6(const TypedNode *node, const TypedType *left_type, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, char (*type_name)[64], bool *outcome2)
{
    if ((node->op == NIPPY_OPERATOR_EQ ||
         node->op == NIPPY_OPERATOR_STRICT_EQ ||
         node->op == NIPPY_OPERATOR_NE) &&
        left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
        if (!buffer_append(buffer,
                           node->op == NIPPY_OPERATOR_NE ? "(!"
                                                         : "(") ||
            !buffer_append(buffer, "nyx_string_equal_at(") ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer,
                             errors) ||
            !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu))",
                            node->pos.line, node->pos.column,
                            node->pos.offset)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            do { *outcome2 = (false); return true; } while (0);
        }
        do { *outcome2 = (true); return true; } while (0);
    }
    {
    bool outcome;

    if (emit_operator_binary_buffer_append4(node, left_type, buffer, program, errors, type_name, &outcome)) {
        do { *outcome2 = (outcome); return true; } while (0);
    }
}
    return false;
}

static void emit_operator_binary_helper(const TypedType *left_type, const TypedType *right_type, const char **helper)
{
    (*helper) = NULL;
    bool ls = nippy_type_is_signed_integer(left_type->kind);
    bool rs = nippy_type_is_signed_integer(right_type->kind);
    bool lu = nippy_type_is_unsigned_integer(left_type->kind);
    bool ru = nippy_type_is_unsigned_integer(right_type->kind);
    bool lf = nippy_type_is_float(left_type->kind);
    bool rf = nippy_type_is_float(right_type->kind);

    if (ls && ru) (*helper) = "nyx_eq_su";
    else if (lu && rs) (*helper) = "nyx_eq_us";
    else if (ls && rf) (*helper) = "nyx_eq_sf";
    else if (lf && rs) (*helper) = "nyx_eq_fs";
    else if (lu && rf) (*helper) = "nyx_eq_uf";
    else if (lf && ru) (*helper) = "nyx_eq_fu";
}

static bool emit_operator_binary_helper9(const TypedNode *node, const TypedType *right_type, const TypedType *left_type, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, bool *outcome)
{
    if ((node->op == NIPPY_OPERATOR_EQ ||
         node->op == NIPPY_OPERATOR_NE) && right_type != NULL &&
        nippy_type_is_numeric(left_type->kind) &&
        nippy_type_is_numeric(right_type->kind)) {
        const char *helper = {0};
        emit_operator_binary_helper(left_type, right_type, &helper);
        if (helper != NULL) {
            if (!buffer_append(buffer, node->op == NIPPY_OPERATOR_NE
                                           ? "(!" : "(") ||
                !buffer_appendf(buffer, "%s(", helper) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported)
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                do { *outcome = (false); return true; } while (0);
            }
            do { *outcome = (true); return true; } while (0);
        }
    }
    return false;
}

static bool emit_operator_binary_utype(const TypedType *left_type, const TypedNode *node, ErrorState *errors, TextBuffer *buffer, char (*type_name)[64], const TypedProgram *program, const char **helper, const char **suffix, const char **op, bool *outcome3)
{
    const char *utype = compute_type_name(left_type->kind);
    (*suffix) = arith_suffix(left_type->kind);
    (*op) = binary_operator_text(node->op);
    (*helper) = NULL;

    if (utype == NULL || (*suffix) == NULL || (*op) == NULL) {
        error_report(errors,
                     MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        do { *outcome3 = (false); return true; } while (0);
    }
    if (node->op == NIPPY_OPERATOR_ADD ||
        node->op == NIPPY_OPERATOR_SUB ||
        node->op == NIPPY_OPERATOR_MUL) {
        if (!buffer_appendf(buffer, "(%s)((%s)(",
                            (*type_name), utype) ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_appendf(buffer, ") %s (%s)(", (*op), utype) ||
            !emit_expression(program, node->right_child, buffer,
                             errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            do { *outcome3 = (false); return true; } while (0);
        }
        do { *outcome3 = (true); return true; } while (0);
    }
    return false;
}

static bool emit_operator_binary_step(const TypedNode *node, const char **helper, TextBuffer *buffer, const char *suffix, const TypedProgram *program, ErrorState *errors)
{
    if (node->op == NIPPY_OPERATOR_DIV) {
        (*helper) = "nyx_div_";
    } else if (node->op == NIPPY_OPERATOR_MOD) {
        (*helper) = "nyx_mod_";
    } else if (node->op == NIPPY_OPERATOR_SHL) {
        (*helper) = "nyx_shl_";
    } else {
        (*helper) = "nyx_shr_";
    }
    if (!buffer_appendf(buffer, "%s%s(", (*helper), suffix) ||
        !emit_expression(program, node->left_child, buffer,
                         errors) ||
        !buffer_append(buffer, ", ") ||
        !emit_expression(program, node->right_child, buffer,
                         errors) ||
        ((node->op == NIPPY_OPERATOR_DIV ||
          node->op == NIPPY_OPERATOR_MOD) &&
         !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu",
                         node->pos.line, node->pos.column,
                         node->pos.offset)) ||
        !buffer_append(buffer, ")")) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_binary_helper13_buffer_append(TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, const char *op, bool *outcome4)
{
    if (!buffer_append(buffer, "(") ||
        !emit_expression(program, node->left_child, buffer,
                         errors) ||
        !buffer_appendf(buffer, " %s ", op) ||
        !emit_expression(program, node->right_child, buffer,
                         errors) ||
        !buffer_append(buffer, ")")) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        do { *outcome4 = (false); return false; } while (0);
    }
    do { *outcome4 = (true); return false; } while (0);
    return true;
}

static bool emit_operator_binary_helper13_operator_binary_step(const TypedNode *node, const char **helper, TextBuffer *buffer, const char *suffix, const TypedProgram *program, ErrorState *errors, bool *outcome4, const char *op)
{
    if (node->op == NIPPY_OPERATOR_DIV ||
        node->op == NIPPY_OPERATOR_MOD ||
        node->op == NIPPY_OPERATOR_SHL ||
        node->op == NIPPY_OPERATOR_SHR) {
        if (!emit_operator_binary_step(node, helper, buffer, suffix, program, errors)) {
do { *outcome4 = (false); return false; } while (0);
}
        do { *outcome4 = (true); return false; } while (0);
    }
    if (!emit_operator_binary_helper13_buffer_append(buffer, program, node, errors, op, outcome4)) {
    return false;
}
    return true;
}

static bool emit_operator_binary_helper13(const TypedType *left_type, const TypedNode *node, ErrorState *errors, TextBuffer *buffer, char (*type_name)[64], const TypedProgram *program, bool *outcome4)
{
    if (nippy_type_is_integer(left_type->kind)) {
        const char *helper = {0};
        const char *suffix = {0};
        const char *op = {0};
        {
    bool outcome3;

    if (emit_operator_binary_utype(left_type, node, errors, buffer, type_name, program, &helper, &suffix, &op, &outcome3)) {
        do { *outcome4 = (outcome3); return true; } while (0);
    }
}
        if (!emit_operator_binary_helper13_operator_binary_step(node, &helper, buffer, suffix, program, errors, outcome4, op)) {
        return true;
    }
    }
    return false;
}

static bool emit_operator_binary_outcome_buffer_append(TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, const char *op, bool *outcome3)
{
    if (!buffer_append(buffer, "(") ||
        !emit_expression(program, node->left_child, buffer,
                         errors) ||
        !buffer_appendf(buffer, " %s ", op) ||
        !emit_expression(program, node->right_child, buffer,
                         errors) ||
        !buffer_append(buffer, ")")) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        do { *outcome3 = (false); return false; } while (0);
    }
    do { *outcome3 = (true); return false; } while (0);
    return true;
}

static bool emit_operator_binary_outcome_op(const TypedType *left_type, const TypedNode *node, ErrorState *errors, bool *outcome3, TextBuffer *buffer, const TypedProgram *program)
{
    if (nippy_type_is_float(left_type->kind)) {
        const char *op = binary_operator_text(node->op);

        if (op == NULL) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            do { *outcome3 = (false); return false; } while (0);
        }
        if (!emit_operator_binary_outcome_buffer_append(buffer, program, node, errors, op, outcome3)) {
        return false;
    }
    }
    return true;
}

static bool emit_operator_binary_outcome(const TypedNode *node, const TypedType *right_type, const TypedType *left_type, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, char (*type_name)[64], bool *outcome3)
{
    {
    bool outcome;

    if (emit_operator_binary_helper9(node, right_type, left_type, buffer, program, errors, &outcome)) {
        do { *outcome3 = (outcome); return true; } while (0);
    }
}
    {
    bool outcome2;

    if (emit_operator_binary_buffer_append6(node, left_type, buffer, program, errors, type_name, &outcome2)) {
        do { *outcome3 = (outcome2); return true; } while (0);
    }
}
    if (!emit_operator_binary_outcome_op(left_type, node, errors, outcome3, buffer, program)) {
        return true;
    }
    {
    bool outcome4;

    if (emit_operator_binary_helper13(left_type, node, errors, buffer, type_name, program, &outcome4)) {
        do { *outcome3 = (outcome4); return true; } while (0);
    }
}
    return false;
}

static bool emit_operator_binary_pair(const TypedType *left_type, TextBuffer *buffer, const TypedNode *node, const TypedNode *left, const TypedNode *right, const TypedProgram *program, ErrorState *errors)
{
    bool pair = (left_type->kind == NIPPY_TYPE_STRING ||
                 left_type->kind == NIPPY_TYPE_DYNAMIC_STRING);
    if (!buffer_append(buffer, node->op == NIPPY_OPERATOR_NE
                                   ? "(!" : "(") ||
        !(pair ? buffer_appendf(buffer, "nyx_equal_pair_%zu_%zu(",
                                left->type_index, right->type_index)
               : buffer_appendf(buffer, "nyx_equal_%zu(",
                                left->type_index)) ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_append(buffer, ", ") ||
        !emit_expression(program, node->right_child, buffer, errors) ||
        !buffer_appendf(buffer, ", __FILE__, %zu, %zu, %zu))",
                        node->pos.line, node->pos.column,
                        node->pos.offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_binary_outcome3(const TypedNode *node, const TypedType *right_type, const TypedType *left_type, TextBuffer *buffer, const TypedProgram *program, ErrorState *errors, char (*type_name)[64])
{
    {
    bool outcome3;

    if (emit_operator_binary_outcome(node, right_type, left_type, buffer, program, errors, type_name, &outcome3)) {
        return outcome3;
    }
}
    {
        const char *op = binary_operator_text(node->op);

        if (op == NULL) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_appendf(buffer, " %s ", op) ||
            !emit_expression(program, node->right_child, buffer,
                             errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
}

static bool emit_operator_binary_left(const TypedProgram *program, const TypedNode *node, ErrorState *errors, TextBuffer *buffer, const TypedType **right_type, const TypedType **left_type, const TypedNode **left, const TypedNode **right, bool *outcome)
{
    (*left) = typed_program_node(program,
                                               node->left_child);
    (*left_type) =
        (*left) == NULL ? NULL
                     : typed_program_type(program, (*left)->type_index);
    (*right) = typed_program_node(program,
                                                node->right_child);
    (*right_type) = (*right) == NULL ? NULL :
        typed_program_type(program, (*right)->type_index);

    if ((*left_type) == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        do { *outcome = (false); return true; } while (0);
    }
    if (node->op == NIPPY_OPERATOR_STRICT_EQ && (*right) != NULL &&
        !types_equivalent(program, (*left)->type_index, (*right)->type_index,
                          64)) {
        if (!emit_operator_binary_buffer_append(buffer, (*left_type), program, node, errors, (*right_type))) {
    do { *outcome = (false); return true; } while (0);
}
        do { *outcome = (true); return true; } while (0);
    }
    return false;
}

static bool emit_operator_binary(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    {
        const TypedType *right_type = {0};
        const TypedType *left_type = {0};
        const TypedNode *left = {0};
        const TypedNode *right = {0};
        {
        bool outcome;

        if (emit_operator_binary_left(program, node, errors, buffer, &right_type, &left_type, &left, &right, &outcome)) {
            return outcome;
        }
    }
        if ((node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE ||
             node->op == NIPPY_OPERATOR_STRICT_EQ) && right_type != NULL &&
            (left_type->kind == NIPPY_TYPE_STRUCT ||
             left_type->kind == NIPPY_TYPE_FIXED_LIST ||
             left_type->kind == NIPPY_TYPE_LIST ||
             left_type->kind == NIPPY_TYPE_RECORD ||
             left_type->kind == NIPPY_TYPE_TABLE ||
             left_type->kind == NIPPY_TYPE_HANDLE ||
             ((left_type->kind == NIPPY_TYPE_STRING ||
               left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) &&
              (right_type->kind == NIPPY_TYPE_STRING ||
               right_type->kind == NIPPY_TYPE_DYNAMIC_STRING) &&
              !types_equivalent(program, left->type_index,
                                right->type_index, 64)))) {
            if (!emit_operator_binary_pair(left_type, buffer, node, left, right, program, errors)) {
        return false;
    }
            return true;
        }
        return emit_operator_binary_outcome3(node, right_type, left_type, buffer, program, errors, &type_name);
    }
}

static bool emit_operator_unary_type_nippy_type_is_float(const TypedType *type, TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, bool *outcome)
{
    if (nippy_type_is_float(type == NULL ? NIPPY_TYPE_UNKNOWN
                                         : type->kind)) {
        if (!buffer_append(buffer, "(-(") ||
            !emit_expression(program, node->left_child, buffer,
                             errors) ||
            !buffer_append(buffer, "))")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            do { *outcome = (false); return false; } while (0);
        }
        do { *outcome = (true); return false; } while (0);
    }
    return true;
}

static bool emit_operator_unary_type(const TypedNode *node, const TypedProgram *program, char (*type_name)[64], ErrorState *errors, TextBuffer *buffer, bool *outcome)
{
    if (node->op == NIPPY_OPERATOR_NEG) {
        const TypedType *type = typed_program_type(program,
                                                   node->type_index);
        const char *utype = type == NULL
                                ? NULL
                                : compute_type_name(type->kind);

        if (!format_c_type(program, node->type_index, (*type_name),
                           sizeof((*type_name)), errors)) {
            do { *outcome = (false); return true; } while (0);
        }
        if (!emit_operator_unary_type_nippy_type_is_float(type, buffer, program, node, errors, outcome)) {
        return true;
    }
        if (utype == NULL ||
            !buffer_appendf(buffer, "(%s)((%s)0 - (%s)(",
                            (*type_name), utype, utype) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            do { *outcome = (false); return true; } while (0);
        }
        do { *outcome = (true); return true; } while (0);
    }
    return false;
}

static bool emit_operator_unary(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    if (node->op == NIPPY_OPERATOR_NOT) {
        if (!buffer_append(buffer, "(!(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "))")) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    {
        bool outcome;

        if (emit_operator_unary_type(node, program, &type_name, errors, buffer, &outcome)) {
            return outcome;
        }
    }
    if (node->op == NIPPY_OPERATOR_BIT_NOT) {
        /* C promotes a narrow operand to int, so ~ on a Uint8 would be
           negative; the cast brings it back to the operand's own type. */
        if (!format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            !buffer_appendf(buffer, "((%s)(~(", type_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")))")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
    return false;
}

static bool emit_operator_struct_constructor(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    if (!format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors) ||
        !buffer_appendf(buffer, "(%s){ ", type_name) ||
        !emit_children_expressions(program, node, buffer, errors) ||
        !buffer_append(buffer, " }")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_function_call(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    char name_text[USER_NAME_CAPACITY];
const char *name = user_c_name(program, node->name_index,
                              name_text, sizeof(name_text));

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    if (!buffer_appendf(buffer, "%s(", name) ||
        !emit_call_arguments(program, node, buffer, errors) ||
        !buffer_append(buffer, ")")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_store_alloc_buffer_append(TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, bool aggregate, char (*element_name)[64])
{
    if (!buffer_append(buffer, "nyx_store_alloc_at(") ||
        !emit_store_reference(program, node, buffer, errors) ||
        !buffer_append(buffer, ", ")) {
        if (!errors->reported) error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (aggregate) {
        if (!buffer_appendf(
                buffer, "&(union { %s nyx_value; }){ .nyx_value = ",
                (*element_name))) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (!buffer_appendf(buffer, "&(%s){ ", (*element_name))) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_operator_store_alloc_store_reference_is_valid(const TypedProgram *program, const TypedNode *node, size_t *element, char (*element_name)[64], ErrorState *errors, bool *aggregate, TextBuffer *buffer)
{
    if (!store_reference_is_valid(program, node) ||
        !handle_element_type(program, node->type_index, element) ||
        !format_c_type(program, (*element), (*element_name),
                       sizeof((*element_name)), errors) ||
        node->left_child == TYPED_NO_NODE) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        }
        return false;
    }
    (*aggregate) = type_is_c_aggregate(program, (*element));
    if (!emit_operator_store_alloc_buffer_append(buffer, program, node, errors, (*aggregate), element_name)) {
        return false;
    }
    return true;
}

static bool emit_operator_store_alloc(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    size_t element;
    bool aggregate;
    char element_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!emit_operator_store_alloc_store_reference_is_valid(program, node, &element, &element_name, errors, &aggregate, buffer)) {
        return false;
    }
    if (!emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_appendf(buffer, " }, sizeof(%s), _Alignof(%s), __FILE__, %zu, %zu, %zu)",
                        element_name, element_name, node->pos.line,
                        node->pos.column, node->pos.offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_handle_read(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    *handled = true;
    if (node->left_child == TYPED_NO_NODE ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors)) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        }
        return false;
    }
    if (!buffer_appendf(buffer, "(*(%s *)nyx_handle_pointer_at(",
                        type_name) ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_appendf(buffer, ", sizeof(%s), __FILE__, %zu, %zu, %zu))",
                        type_name, node->pos.line, node->pos.column,
                        node->pos.offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static bool emit_operator_handle_write_buffer_append(TextBuffer *buffer, const TypedProgram *program, const TypedNode *node, ErrorState *errors, bool aggregate, char (*element_name)[64])
{
    if (!buffer_append(buffer, "((void)nyx_handle_write_at(") ||
        !emit_expression(program, node->left_child, buffer, errors) ||
        !buffer_append(buffer, ", ")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    if (aggregate) {
        if (!buffer_appendf(
                buffer, "&(union { %s nyx_value; }){ .nyx_value = ",
                (*element_name))) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else if (!buffer_appendf(buffer, "&(%s){ ", (*element_name))) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}

static bool emit_operator_handle_write_handle_element(const TypedNode *handle, const TypedType *result, const TypedNode *node, const TypedProgram *program, size_t *element, char (*element_name)[64], ErrorState *errors, bool *aggregate, TextBuffer *buffer)
{
    if (handle == NULL || result == NULL ||
        result->kind != NIPPY_TYPE_NIL ||
        node->right_child == TYPED_NO_NODE ||
        !handle_element_type(program, handle->type_index, element) ||
        !format_c_type(program, (*element), (*element_name),
                       sizeof((*element_name)), errors)) {
        if (!errors->reported) {
            error_report(errors,
                         MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
        }
        return false;
    }
    (*aggregate) = type_is_c_aggregate(program, (*element));
    if (!emit_operator_handle_write_buffer_append(buffer, program, node, errors, (*aggregate), element_name)) {
        return false;
    }
    return true;
}

static bool emit_operator_handle_write(const TypedProgram *program, const TypedNode *node, TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    *handled = true;
    const TypedNode *handle =
        typed_program_node(program, node->left_child);
    const TypedType *result = typed_program_type(program, node->type_index);
    size_t element;
    bool aggregate;
    char element_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!emit_operator_handle_write_handle_element(handle, result, node, program, &element, &element_name, errors, &aggregate, buffer)) {
        return false;
    }
    if (!emit_expression(program, node->right_child, buffer, errors) ||
        !buffer_appendf(buffer, " }, sizeof(%s), __FILE__, %zu, %zu, %zu), (nyx_nil)0)",
                        element_name, node->pos.line, node->pos.column,
                        node->pos.offset)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}

static const struct {
    int key;
    EmitExprOperatorsHandler handler;
} emit_expr_operators_handlers[] = {
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

    for (size_t i = 0; i < NIPPY_COUNT_OF(emit_expr_operators_handlers); i++) {
        if (emit_expr_operators_handlers[i].key == (int)node->kind) {
            return emit_expr_operators_handlers[i].handler(program, node, buffer, errors, handled);
        }
    }
    return true;
}
