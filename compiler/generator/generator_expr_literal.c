#include "generator_internal.h"

bool emit_expr_literals(const TypedProgram *program, const TypedNode *node,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    char type_name[NIPPY_IDENTIFIER_MAX + 32];
    if (node->kind == TYPED_INTEGER_LITERAL) {
        *handled = true;
        {
            const TypedType *type = typed_program_type(program,
                                                       node->type_index);

            if (type != NULL &&
                (type->kind == NIPPY_TYPE_INT32 ||
                 type->kind == NIPPY_TYPE_BYTE)) {
                if (!buffer_appendf(buffer, "%d", node->integer_value)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                return true;
            }
            if (type == NULL ||
                !format_c_type(program, node->type_index, type_name,
                               sizeof(type_name), errors) ||
                !buffer_appendf(buffer, "((%s)%lluULL)", type_name,
                                (unsigned long long)node->integer_value_u)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        return true;
    }
    if (node->kind == TYPED_FLOAT_LITERAL) {
        *handled = true;
        if (!format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            !buffer_appendf(buffer, "((%s)%.17g)", type_name,
                            node->float_value)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BOOL_LITERAL) {
        *handled = true;
        if (!buffer_append(buffer, node->integer_value ? "true" : "false")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_IDENTIFIER) {
        *handled = true;
        if (node->is_self) {
            if (!buffer_append(buffer, "(*nyx_self)")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!buffer_append(buffer, name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_NIL_LITERAL) {
        *handled = true;
        if (!buffer_append(buffer, "((nyx_nil)0)")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SUM_INJECT) {
        *handled = true;
        const TypedType *union_type = typed_program_type(program, node->type_index);
        const TypedType *alternative;
        char union_name[NIPPY_IDENTIFIER_MAX + 32];

        if (union_type == NULL || union_type->kind != NIPPY_TYPE_UNION ||
            node->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        alternative = union_alternative_type(program, union_type,
                                             (size_t)node->integer_value);
        if (alternative == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!format_c_type(program, node->type_index, union_name,
                           sizeof(union_name), errors)) {
            return false;
        }
        if (alternative->kind == NIPPY_TYPE_NIL) {
            if (!buffer_appendf(buffer, "((%s){ .tag = %d })", union_name,
                                node->integer_value)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (node->left_child == TYPED_NO_NODE) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "((%s){ .tag = %d, .value.v%d = ",
                            union_name, node->integer_value,
                            node->integer_value) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, " })")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SUM_PAYLOAD) {
        const TypedType *payload_type = typed_program_type(program,
                                                           node->type_index);

        *handled = true;
        if (node->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (payload_type != NULL && payload_type->kind == NIPPY_TYPE_NIL) {
            /* Nil has no payload member in the union, only a tag; the value
               of the narrowed alternative is the unit value itself. */
            if (!buffer_append(buffer, "((void)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "), (nyx_nil)0)")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ".value.v%d)", node->integer_value)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_IF_EXPRESSION) {
        *handled = true;
        size_t else_expression = *(const size_t *)list_get(
            &program->children, node->children_start);

        if (!buffer_append(buffer, "(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, " ? ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, " : ") ||
            !emit_expression(program, else_expression, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_IS) {
        *handled = true;
        const TypedNode *value = typed_program_node(program, node->left_child);
        const TypedType *value_type =
            value == NULL ? NULL : typed_program_type(program, value->type_index);

        if (value == NULL || value_type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (value_type->kind == NIPPY_TYPE_UNION) {
            size_t tag = 0;
            bool runtime = true;

            if (node->auxiliary_type != TYPED_NO_NODE) {
                runtime = find_union_alternative(program, value_type,
                                                 node->auxiliary_type, &tag);
            } else if (node->integer_value >= 0 &&
                       (size_t)node->integer_value < value_type->child_count) {
                tag = (size_t)node->integer_value;
            } else {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (!runtime) {
                /* A tested type absent from the union is a decided false. */
                if (!buffer_append(buffer, "false")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
                return true;
            }
            if (!buffer_append(buffer, "(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_appendf(buffer, ".tag == %zu)", tag)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        {
            /* A type test on a non-union value is decided statically. */
            bool result;

            if (node->auxiliary_type != TYPED_NO_NODE) {
                result = types_equivalent(program, value->type_index,
                                          node->auxiliary_type, 16);
            } else if (node->integer_value >= 0) {
                result = node->integer_value != 0;
            } else {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            if (!buffer_append(buffer, result ? "true" : "false")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
    }
    return true;
}
