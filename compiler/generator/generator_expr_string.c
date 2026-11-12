#include "generator_internal.h"

bool emit_expr_strings(const TypedProgram *program, const TypedNode *node,
                size_t node_index,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    if (node->kind == TYPED_STRING_LITERAL) {
        *handled = true;
        const TypedType *type = typed_program_type(program, node->type_index);
        char type_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t i;

        if (type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_appendf(buffer,
                                "(nyx_string){ nyx_dyn_str_%zu, 0, 0, "
                                "%zu, 0 }",
                                node_index, node->string_length)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (type->kind != NIPPY_TYPE_STRING ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors)) {
            if (!errors->reported) {
                error_report(errors,
                             MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            }
            return false;
        }
        if (!buffer_appendf(buffer, "(%s){ { ", type_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < node->string_length; i++) {
            const unsigned char *byte = list_get(
                &program->bytes, node->string_offset + i);

            if (byte == NULL ||
                !buffer_appendf(buffer, "%u, ", (unsigned)*byte)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(buffer, "0 }, %zu }", node->string_length)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_LEN) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);

        if (receiver_type != NULL &&
            receiver_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_append(buffer, "nyx_string_len(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        } else if (!buffer_append(buffer, "((int32_t)(") ||
                   !emit_expression(program, node->left_child, buffer,
                                    errors) ||
                   !buffer_append(buffer, ").length)")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_AT) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type = receiver == NULL
                                             ? NULL
                                             : typed_program_type(
                                                   program,
                                                   receiver->type_index);

        if (receiver_type == NULL ||
            (receiver_type->kind != NIPPY_TYPE_STRING &&
             receiver_type->kind != NIPPY_TYPE_DYNAMIC_STRING)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_append(buffer, "nyx_string_byte_at(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
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
        if (!buffer_appendf(buffer, "nyx_string_%zu_at(",
                            receiver_type->fixed_length) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_SLICE) {
        *handled = true;
        if (node->left_child == TYPED_NO_NODE ||
            node->children_count != 2) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        {
            size_t start = *(const size_t *)list_get(
                &program->children, node->children_start);
            size_t length = *(const size_t *)list_get(
                &program->children, node->children_start + 1);

            if (!buffer_append(buffer, "nyx_string_slice(") ||
                !emit_expression(program, node->left_child, buffer,
                                 errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, start, buffer, errors) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, length, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        return true;
    }
    if (node->kind == TYPED_STRING_CONCAT ||
        node->kind == TYPED_STRING_COPY) {
        *handled = true;
        if (node->left_child == TYPED_NO_NODE ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "nyx_string_%s(&nyx_stores[%zu], "
                                    "%zu, ",
                            node->kind == TYPED_STRING_CONCAT ? "concat"
                                                              : "copy",
                            node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (node->kind == TYPED_STRING_CONCAT) {
            if (node->right_child == TYPED_NO_NODE ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer,
                                 errors)) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        if (!buffer_append(buffer, ")")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_STRING_FROM_FIXED) {
        *handled = true;
        const TypedNode *source = typed_program_node(program,
                                                     node->left_child);
        const TypedType *source_type =
            source == NULL
                ? NULL
                : typed_program_type(program, source->type_index);

        if (source_type == NULL || source_type->kind != NIPPY_TYPE_STRING ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "nyx_string_from_fixed_%zu(&nyx_stores[%zu], "
                            "%zu, ",
                            source_type->fixed_length, node->store_id,
                            node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_CREATE) {
        *handled = true;
        if (node->left_child == TYPED_NO_NODE ||
            !store_id_is_valid(program, node->store_id)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "nyx_builder_create(&nyx_stores[%zu], %zu, ",
                            node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_APPEND ||
        node->kind == TYPED_BUILDER_APPEND_BYTE) {
        *handled = true;
        if (node->left_child == TYPED_NO_NODE ||
            node->right_child == TYPED_NO_NODE) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_append(buffer,
                           node->kind == TYPED_BUILDER_APPEND
                               ? "nyx_builder_append("
                               : "nyx_builder_append_byte(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_LEN) {
        *handled = true;
        if (node->left_child == TYPED_NO_NODE ||
            !buffer_append(buffer, "nyx_builder_len(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_BUILDER_FINISH) {
        *handled = true;
        if (node->left_child == TYPED_NO_NODE ||
            !buffer_append(buffer, "nyx_builder_finish(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    return true;
}
