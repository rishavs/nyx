#include "generator_internal.h"

bool emit_expr_operators(const TypedProgram *program, const TypedNode *node,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (node->kind == TYPED_BINARY) {
        *handled = true;
        if (node->op == NIPPY_OPERATOR_EQ || node->op == NIPPY_OPERATOR_NE) {
            const TypedNode *left = typed_program_node(program,
                                                       node->left_child);
            const TypedType *left_type =
                left == NULL
                    ? NULL
                    : typed_program_type(program, left->type_index);

            if (left_type != NULL &&
                left_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
                if (!buffer_append(buffer,
                                   node->op == NIPPY_OPERATOR_NE ? "(!"
                                                                 : "(") ||
                    !buffer_append(buffer, "nyx_string_equal(") ||
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
            if (left_type != NULL &&
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
        }
        if (node->op == NIPPY_OPERATOR_SUB ||
            node->op == NIPPY_OPERATOR_MUL) {
            const char *op = node->op == NIPPY_OPERATOR_SUB ? "-" : "*";

            if (!buffer_append(buffer, "(int32_t)((uint32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, ") %s (uint32_t)(", op) ||
                !emit_expression(program, node->right_child, buffer, errors) ||
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
            const char *helper = node->op == NIPPY_OPERATOR_DIV
                                     ? "nyx_div_i32"
                                     : node->op == NIPPY_OPERATOR_MOD
                                           ? "nyx_mod_i32"
                                           : node->op == NIPPY_OPERATOR_SHL
                                                 ? "nyx_shl_i32"
                                                 : "nyx_shr_i32";

            if (!buffer_appendf(buffer, "%s(", helper) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (node->op == NIPPY_OPERATOR_ADD) {
            if (!buffer_append(buffer, "(int32_t)((uint32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ") + (uint32_t)(") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
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
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, " %s ", op) ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
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
            if (!buffer_append(buffer, "(int32_t)(0u - (uint32_t)(") ||
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
        if (node->op == NIPPY_OPERATOR_BIT_NOT) {
            if (!buffer_append(buffer, "(~(") ||
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
        const char *name = typed_program_symbol_text(program, node->name_index);

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_appendf(buffer, "%s(", name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
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
                            "nyx_store_alloc(&nyx_stores[%zu], %zu, ",
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
            !buffer_appendf(buffer, " }, sizeof(%s), _Alignof(%s))",
                            element_name, element_name)) {
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
                            "((void)nyx_store_reset(&nyx_stores[%zu]), "
                            "(nyx_nil)0)",
                            node->store_id)) {
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
        if (!buffer_appendf(buffer, "(*(%s *)nyx_handle_pointer(",
                            type_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)))", type_name)) {
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
        if (!buffer_append(buffer, "((void)nyx_handle_write(") ||
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
            !buffer_appendf(buffer, " }, sizeof(%s)), (nyx_nil)0)",
                            element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    return true;
}
