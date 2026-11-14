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



bool emit_expr_operators(const TypedProgram *program, const TypedNode *node,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (node->kind == TYPED_NUMERIC_CONVERSION) {
        *handled = true;
        {
            const TypedType *target = typed_program_type(program,
                                                         node->type_index);
            const TypedType *source = typed_program_type(program,
                                                         node->auxiliary_type);
            const char *suffix;

            if (target == NULL || source == NULL ||
                !format_c_type(program, node->type_index, type_name,
                               sizeof(type_name), errors)) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (target->kind == source->kind) {
                /* Same type, or a constant already converted by the checker. */
                return emit_expression(program, node->left_child, buffer,
                                       errors);
            }
            if (nippy_type_is_float(source->kind) &&
                nippy_type_is_integer(target->kind)) {
                suffix = arith_suffix(target->kind);
                if (suffix == NULL ||
                    !buffer_appendf(buffer, "nyx_f2i_%s((double)(", suffix) ||
                    !emit_expression(program, node->left_child, buffer,
                                     errors) ||
                    !buffer_appendf(buffer, "), __FILE__, %zu, %zu, %zu)",
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
            if (nippy_type_is_integer(source->kind) &&
                nippy_type_is_integer(target->kind) &&
                !integer_conversion_always_fits(source->kind, target->kind)) {
                bool from_signed = nippy_type_is_signed_integer(source->kind);

                suffix = arith_suffix(target->kind);
                if (suffix == NULL ||
                    !buffer_appendf(buffer, "nyx_c%c_%s((%s)(",
                                    from_signed ? 's' : 'u', suffix,
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
                    return false;
                }
                return true;
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
    if (node->kind == TYPED_BINARY) {
        *handled = true;
        {
            const TypedNode *left = typed_program_node(program,
                                                       node->left_child);
            const TypedType *left_type =
                left == NULL ? NULL
                             : typed_program_type(program, left->type_index);
            const TypedNode *right = typed_program_node(program,
                                                        node->right_child);
            const TypedType *right_type = right == NULL ? NULL :
                typed_program_type(program, right->type_index);

            if (left_type == NULL) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (node->op == NIPPY_OPERATOR_STRICT_EQ && right != NULL &&
                !types_equivalent(program, left->type_index, right->type_index,
                                  64)) {
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
            if ((node->op == NIPPY_OPERATOR_EQ ||
                 node->op == NIPPY_OPERATOR_NE) && right_type != NULL &&
                nippy_type_is_numeric(left_type->kind) &&
                nippy_type_is_numeric(right_type->kind)) {
                const char *helper = NULL;
                bool ls = nippy_type_is_signed_integer(left_type->kind);
                bool rs = nippy_type_is_signed_integer(right_type->kind);
                bool lu = nippy_type_is_unsigned_integer(left_type->kind);
                bool ru = nippy_type_is_unsigned_integer(right_type->kind);
                bool lf = nippy_type_is_float(left_type->kind);
                bool rf = nippy_type_is_float(right_type->kind);

                if (ls && ru) helper = "nyx_eq_su";
                else if (lu && rs) helper = "nyx_eq_us";
                else if (ls && rf) helper = "nyx_eq_sf";
                else if (lf && rs) helper = "nyx_eq_fs";
                else if (lu && rf) helper = "nyx_eq_uf";
                else if (lf && ru) helper = "nyx_eq_fu";
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
                        return false;
                    }
                    return true;
                }
            }
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
                    return false;
                }
                return true;
            }
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
                    return false;
                }
                return true;
            }
            if (!format_c_type(program, node->type_index, type_name,
                               sizeof(type_name), errors)) {
                return false;
            }
            if (nippy_type_is_float(left_type->kind)) {
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
            if (nippy_type_is_integer(left_type->kind)) {
                const char *utype = compute_type_name(left_type->kind);
                const char *suffix = arith_suffix(left_type->kind);
                const char *op = binary_operator_text(node->op);
                const char *helper = NULL;

                if (utype == NULL || suffix == NULL || op == NULL) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                    return false;
                }
                if (node->op == NIPPY_OPERATOR_ADD ||
                    node->op == NIPPY_OPERATOR_SUB ||
                    node->op == NIPPY_OPERATOR_MUL) {
                    if (!buffer_appendf(buffer, "(%s)((%s)(",
                                        type_name, utype) ||
                        !emit_expression(program, node->left_child, buffer,
                                         errors) ||
                        !buffer_appendf(buffer, ") %s (%s)(", op, utype) ||
                        !emit_expression(program, node->right_child, buffer,
                                         errors) ||
                        !buffer_append(buffer, "))")) {
                        if (!errors->reported) {
                            error_report(errors,
                                         MESSAGE_GENERATOR_OUT_OF_MEMORY);
                        }
                        return false;
                    }
                    return true;
                }
                if (node->op == NIPPY_OPERATOR_DIV ||
                    node->op == NIPPY_OPERATOR_MOD ||
                    node->op == NIPPY_OPERATOR_SHL ||
                    node->op == NIPPY_OPERATOR_SHR) {
                    if (node->op == NIPPY_OPERATOR_DIV) {
                        helper = "nyx_div_";
                    } else if (node->op == NIPPY_OPERATOR_MOD) {
                        helper = "nyx_mod_";
                    } else if (node->op == NIPPY_OPERATOR_SHL) {
                        helper = "nyx_shl_";
                    } else {
                        helper = "nyx_shr_";
                    }
                    if (!buffer_appendf(buffer, "%s%s(", helper, suffix) ||
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
    }
    if (node->kind == TYPED_UNARY) {
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
        if (node->op == NIPPY_OPERATOR_NEG) {
            const TypedType *type = typed_program_type(program,
                                                       node->type_index);
            const char *utype = type == NULL
                                    ? NULL
                                    : compute_type_name(type->kind);

            if (!format_c_type(program, node->type_index, type_name,
                               sizeof(type_name), errors)) {
                return false;
            }
            if (nippy_type_is_float(type == NULL ? NIPPY_TYPE_UNKNOWN
                                                 : type->kind)) {
                if (!buffer_append(buffer, "(-(") ||
                    !emit_expression(program, node->left_child, buffer,
                                     errors) ||
                    !buffer_append(buffer, "))")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                return true;
            }
            if (utype == NULL ||
                !buffer_appendf(buffer, "(%s)((%s)0 - (%s)(",
                                type_name, utype, utype) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                }
                return false;
            }
            return true;
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
    if (node->kind == TYPED_STRUCT_CONSTRUCTOR) {
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
    if (node->kind == TYPED_FUNCTION_CALL) {
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
    if (node->kind == TYPED_STORE_ALLOC) {
        *handled = true;
        size_t element;
        bool aggregate;
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!store_id_is_valid(program, node->store_id) ||
            !handle_element_type(program, node->type_index, &element) ||
            !format_c_type(program, element, element_name,
                           sizeof(element_name), errors) ||
            node->left_child == TYPED_NO_NODE) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        aggregate = type_is_c_aggregate(program, element);
        if (!buffer_appendf(buffer,
                            "nyx_store_alloc_at(&nyx_stores[%zu], %zu, ",
                            node->store_id, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (aggregate) {
            if (!buffer_appendf(
                    buffer, "&(union { %s nyx_value; }){ .nyx_value = ",
                    element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_appendf(buffer, "&(%s){ ", element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
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
    if (node->kind == TYPED_STORE_RESET) {
        *handled = true;
        const TypedType *type = typed_program_type(program, node->type_index);

        if (type == NULL || type->kind != NIPPY_TYPE_NIL ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "((void)nyx_store_reset_at(&nyx_stores[%zu], __FILE__, %zu, %zu, %zu), "
                            "(nyx_nil)0)",
                            node->store_id, node->pos.line, node->pos.column,
                            node->pos.offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_HANDLE_READ) {
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
    if (node->kind == TYPED_HANDLE_WRITE) {
        *handled = true;
        const TypedNode *handle =
            typed_program_node(program, node->left_child);
        const TypedType *result = typed_program_type(program, node->type_index);
        size_t element;
        bool aggregate;
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (handle == NULL || result == NULL ||
            result->kind != NIPPY_TYPE_NIL ||
            node->right_child == TYPED_NO_NODE ||
            !handle_element_type(program, handle->type_index, &element) ||
            !format_c_type(program, element, element_name,
                           sizeof(element_name), errors)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        aggregate = type_is_c_aggregate(program, element);
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
                    element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_appendf(buffer, "&(%s){ ", element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
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
    return true;
}
