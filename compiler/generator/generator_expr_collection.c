#include "generator_internal.h"

bool emit_expr_collections(const TypedProgram *program, const TypedNode *node,
                size_t node_index,
                TextBuffer *buffer, ErrorState *errors, bool *handled)
{
    if (node->kind == TYPED_LIST_LITERAL) {
        *handled = true;
        char type_name[NIPPY_IDENTIFIER_MAX + 32];
        const TypedType *type = typed_program_type(program, node->type_index);

        if (type == NULL || type->kind != NIPPY_TYPE_FIXED_LIST ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (node->children_count == 0) {
            if (!buffer_appendf(buffer, "(%s){ { 0 }, 0 }", type_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "(%s){ { ", type_name) ||
            !emit_children_expressions(program, node, buffer, errors) ||
            !buffer_appendf(buffer, " }, %zu }", node->children_count)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_LEN) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);

        if (receiver_type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_append(buffer, "((int32_t)(") ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ").length)")) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_append(buffer, "nyx_list_len(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_GET_OPTIONAL) {
        *handled = true;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);

        if (receiver_type == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            receiver_type->kind == NIPPY_TYPE_SLICE
                                ? "nyx_slice_get_optional_%zu("
                                : "nyx_list_get_optional_%zu(",
                            node_index) ||
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
    if (node->kind == TYPED_LIST_GET) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const TypedType *element_type =
            typed_program_type(program, node->type_index);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (receiver_type == NULL || element_type == NULL ||
            !format_c_type(program, node->type_index, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(buffer, "nyx_list_%zu_get(",
                                receiver->type_index) ||
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
        if (!buffer_appendf(buffer, "(*(%s *)nyx_list_data(",
                            element_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)))", element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_SET) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const TypedType *element_type =
            typed_program_type(program, node->auxiliary_type);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t index;
        size_t value;

        if (receiver_type == NULL || element_type == NULL ||
            node->children_count != 2 ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        index = *(const size_t *)list_get(&program->children,
                                          node->children_start);
        value = *(const size_t *)list_get(&program->children,
                                          node->children_start + 1);
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            const char *name = typed_program_symbol_text(
                program, node->name_index);

            if (!valid_identifier(name) ||
                !buffer_appendf(buffer, "(*nyx_list_%zu_at(&%s, ",
                                receiver->type_index, name) ||
                !emit_expression(program, index, buffer, errors) ||
                !buffer_append(buffer, ") = ") ||
                !emit_expression(program, value, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer, "(*(%s *)nyx_list_data(",
                            element_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, index, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)) = ", element_name) ||
            !emit_expression(program, value, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_PUSH) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const TypedType *element_type =
            typed_program_type(program, node->auxiliary_type);
        const char *name = typed_program_symbol_text(program,
                                                      node->name_index);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (receiver_type == NULL || element_type == NULL ||
            !valid_identifier(name) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(buffer, "nyx_list_%zu_push(&%s, ",
                                receiver->type_index, name) ||
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
        if (!buffer_appendf(buffer, "nyx_list_push(%s, (%s[]){ ", name,
                            element_name) ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, " }, sizeof(%s), _Alignof(%s))",
                            element_name, element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_POP) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        const char *name = typed_program_symbol_text(program,
                                                      node->name_index);

        if (receiver_type == NULL || !valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "nyx_list_pop_%zu(", node_index)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (receiver_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(buffer, "&%s", name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (!buffer_append(buffer, name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_append(buffer, ")")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_LIST_CREATE) {
        *handled = true;
        const TypedType *element_type =
            typed_program_type(program, node->auxiliary_type);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (element_type == NULL ||
            !store_id_is_valid(program, node->store_id) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer,
                            "nyx_list_create(&nyx_stores[%zu], %zu, ",
                            node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s), _Alignof(%s))",
                            element_name, element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SLICE_CREATE) {
        *handled = true;
        const TypedNode *receiver =
            typed_program_node(program, node->left_child);
        const TypedType *receiver_type =
            receiver == NULL
                ? NULL
                : typed_program_type(program, receiver->type_index);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t start;
        size_t length;

        if (receiver_type == NULL || node->children_count != 2 ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        start = *(const size_t *)list_get(&program->children,
                                          node->children_start);
        length = *(const size_t *)list_get(&program->children,
                                           node->children_start + 1);
        if (!buffer_appendf(buffer, "nyx_slice_%s(",
                            receiver_type->kind == NIPPY_TYPE_SLICE
                                ? "sub"
                                : "from_list") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, start, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, length, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s))", element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SLICE_LEN) {
        *handled = true;
        if (!buffer_append(buffer, "nyx_slice_len(") ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_SLICE_GET) {
        *handled = true;
        char element_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!format_c_type(program, node->type_index, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "(*(%s *)nyx_slice_data(",
                            element_name) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ", ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_appendf(buffer, ", sizeof(%s)))", element_name)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_RECORD_LITERAL) {
        *handled = true;
        const TypedType *type = typed_program_type(program, node->type_index);
        char record_name[64];
        char key_name[64];
        char value_name[64];
        size_t key_type;
        size_t value_type;
        size_t i;

        if (type == NULL || type->child_count != 2 ||
            !format_c_type(program, node->type_index, record_name,
                           sizeof(record_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        key_type = *(const size_t *)list_get(&program->type_children,
                                             type->first_child);
        value_type = *(const size_t *)list_get(&program->type_children,
                                               type->first_child + 1);
        if (!format_c_type(program, key_type, key_name, sizeof(key_name),
                           errors) ||
            !format_c_type(program, value_type, value_name,
                           sizeof(value_name), errors)) {
            return false;
        }
        if (node->children_count == 0) {
            if (!buffer_appendf(buffer, "(%s){0}", record_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            return true;
        }
        if (!buffer_appendf(buffer,
                            "nyx_record_%zu_literal((%s[]){ ",
                            node->type_index, key_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i + 1 < node->children_count; i += 2) {
            size_t key = *(const size_t *)list_get(&program->children,
                                                   node->children_start + i);

            if (i != 0 && !buffer_append(buffer, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!emit_expression(program, key, buffer, errors)) {
                return false;
            }
        }
        if (!buffer_appendf(buffer, " }, (%s[]){ ", value_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 1; i < node->children_count; i += 2) {
            size_t value = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);

            if (i != 1 && !buffer_append(buffer, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!emit_expression(program, value, buffer, errors)) {
                return false;
            }
        }
        if (!buffer_appendf(buffer, " }, %zu)", node->children_count / 2)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_TABLE_CREATE) {
        *handled = true;
        if (!buffer_appendf(buffer,
                            "nyx_table_%zu_create(&nyx_stores[%zu], %zu, ",
                            node->type_index, node->store_id, node->store_id) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_TABLE_GET_OPTIONAL) {
        *handled = true;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);

        if (receiver == NULL ||
            !buffer_appendf(buffer, "nyx_table_%zu_get_optional_%zu(",
                            receiver->type_index, node_index) ||
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
    if (node->kind == TYPED_TABLE_GET) {
        *handled = true;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);

        if (receiver == NULL ||
            !buffer_appendf(buffer, "nyx_table_%zu_get_required(",
                            receiver->type_index) ||
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
    if (node->kind == TYPED_TABLE_HAS ||
        node->kind == TYPED_TABLE_REMOVE ||
        node->kind == TYPED_TABLE_LEN) {
        *handled = true;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);
        const char *suffix = node->kind == TYPED_TABLE_HAS
                                 ? "has"
                                 : (node->kind == TYPED_TABLE_REMOVE
                                        ? "remove"
                                        : "len");

        if (receiver == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (node->kind == TYPED_TABLE_LEN) {
            if (!buffer_appendf(buffer, "nyx_table_%zu_len(",
                                receiver->type_index) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            if (!buffer_appendf(buffer, "nyx_table_%zu_%s(",
                                receiver->type_index, suffix) ||
                (node->kind == TYPED_TABLE_REMOVE &&
                 !buffer_append(buffer, "&(")) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                (node->kind == TYPED_TABLE_REMOVE &&
                 !buffer_append(buffer, ")")) ||
                !buffer_append(buffer, ", ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        return true;
    }
    if (node->kind == TYPED_RECORD_GET) {
        *handled = true;
        const TypedType *receiver_type = NULL;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);

        receiver_type = receiver == NULL
                            ? NULL
                            : typed_program_type(program,
                                                 receiver->type_index);
        if (receiver_type == NULL ||
            receiver_type->kind != NIPPY_TYPE_RECORD) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!buffer_appendf(buffer, "nyx_record_%zu_get_required(&(",
                            receiver->type_index) ||
            !emit_expression(program, node->left_child, buffer, errors) ||
            !buffer_append(buffer, "), ") ||
            !emit_expression(program, node->right_child, buffer, errors) ||
            !buffer_append(buffer, ")")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (node->kind == TYPED_RECORD_HAS ||
        node->kind == TYPED_RECORD_REMOVE ||
        node->kind == TYPED_RECORD_LEN) {
        *handled = true;
        const TypedNode *receiver = typed_program_node(program,
                                                       node->left_child);
        const char *suffix = node->kind == TYPED_RECORD_HAS
                                 ? "has"
                                 : (node->kind == TYPED_RECORD_REMOVE
                                        ? "remove"
                                        : "len");
        const char *prefix = node->kind == TYPED_RECORD_LEN
                                 ? "nyx_record_%zu_len(&("
                                 : "nyx_record_%zu_%s(&(";

        if (receiver == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (node->kind == TYPED_RECORD_LEN) {
            if (!buffer_appendf(buffer, prefix, receiver->type_index) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "))")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            if (!buffer_appendf(buffer, prefix, receiver->type_index,
                                suffix) ||
                !emit_expression(program, node->left_child, buffer, errors) ||
                !buffer_append(buffer, "), ") ||
                !emit_expression(program, node->right_child, buffer, errors) ||
                !buffer_append(buffer, ")")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        return true;
    }
    return true;
}
