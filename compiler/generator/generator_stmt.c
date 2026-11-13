#include "generator_internal.h"


bool emit_map_for(const TypedProgram *program, const TypedNode *node,
                          const TypedType *iterable_type, size_t depth,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    char key_name_text[USER_NAME_CAPACITY];
    const char *key_name = user_c_name(program, node->name_index,
                                  key_name_text, sizeof(key_name_text));
    char value_name_text[USER_NAME_CAPACITY];
    const char *value_name =
        node->second_name_index == TYPED_NO_NODE
            ? NULL
            : user_c_name(program, node->second_name_index, value_name_text,
                          sizeof(value_name_text));
    size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                iterable_type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, iterable_type->first_child + 1);
    char key_c[NIPPY_IDENTIFIER_MAX + 32];
    char value_c[NIPPY_IDENTIFIER_MAX + 32];
    size_t offset = node->pos.offset;

    if (key_name == NULL || !valid_identifier(key_name) ||
        (value_name != NULL && !valid_identifier(value_name)) ||
        !format_c_type(program, key_type, key_c, sizeof(key_c), errors) ||
        !format_c_type(program, value_type, value_c, sizeof(value_c), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (iterable_type->kind == NIPPY_TYPE_RECORD) {
        size_t buckets = iterable_type->fixed_length == 0
                             ? 1
                             : iterable_type->fixed_length + 1;

        if (!append_indent(source, depth) ||
            !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        {
            size_t child = node->left_child;
            const TypedNode *iter = typed_program_node(program, child);
            char record_name[64];

            if (iter == NULL ||
                !format_c_type(program, iter->type_index, record_name,
                               sizeof(record_name), errors) ||
                !buffer_appendf(source, "%s nyx_for_src_%zu = ",
                                record_name, offset) ||
                !emit_expression(program, child, source, errors) ||
                !buffer_append(source, ";\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "for (size_t nyx_for_i_%zu = 0; "
                            "nyx_for_i_%zu < %zu; nyx_for_i_%zu++) {\n",
                            offset, offset, buckets, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "if (nyx_for_src_%zu.states[nyx_for_i_%zu] "
                            "!= 1) continue;\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nyx_for_src_%zu.keys["
                                    "nyx_for_i_%zu];\n",
                            key_c, key_name, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", key_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (value_name != NULL) {
            if (!append_indent(source, depth + 2) ||
                !buffer_appendf(source, "%s %s = nyx_for_src_%zu.values["
                                        "nyx_for_i_%zu];\n",
                                value_c, value_name, offset, offset) ||
                !append_indent(source, depth + 2) ||
                !buffer_appendf(source, "(void)%s;\n", value_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    } else {
        const TypedNode *iterable = typed_program_node(program,
                                                       node->left_child);
        size_t table_type = iterable == NULL ? TYPED_NO_NODE
                                             : iterable->type_index;

        if (table_type == TYPED_NO_NODE ||
            !append_indent(source, depth) ||
            !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "nyx_table nyx_for_src_%zu = ",
                            offset) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "for (size_t nyx_for_i_%zu = 0;; "
                            "nyx_for_i_%zu++) {\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "nyx_table_state_%zu *nyx_for_state_%zu = "
                            "nyx_table_%zu_state(nyx_for_src_%zu);\n",
                            table_type, offset, table_type, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "if (nyx_for_i_%zu >= "
                            "nyx_for_state_%zu->buckets) break;\n",
                            offset, offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "nyx_store *nyx_for_store_%zu = "
                            "&nyx_stores[nyx_for_src_%zu.store];\n",
                            offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "uint8_t *nyx_for_states_%zu = (uint8_t *)"
                            "(void *)(nyx_for_store_%zu->data + "
                            "nyx_for_state_%zu->states_offset);\n"
                            "        if (nyx_for_states_%zu[nyx_for_i_%zu]"
                            " != 1) continue;\n",
                            offset, offset, offset, offset, offset)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!append_indent(source, depth + 2) ||
            !buffer_appendf(source,
                            "%s *nyx_for_keys_%zu = (%s *)(void *)"
                            "(nyx_for_store_%zu->data + "
                            "nyx_for_state_%zu->keys_offset);\n",
                            key_c, offset, key_c, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "%s %s = nyx_for_keys_%zu["
                                    "nyx_for_i_%zu];\n",
                            key_c, key_name, offset, offset) ||
            !append_indent(source, depth + 2) ||
            !buffer_appendf(source, "(void)%s;\n", key_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (value_name != NULL) {
            if (!append_indent(source, depth + 2) ||
                !buffer_appendf(source,
                                "%s *nyx_for_values_%zu = (%s *)(void *)"
                                "(nyx_for_store_%zu->data + "
                                "nyx_for_state_%zu->values_offset);\n",
                                value_c, offset, value_c, offset, offset) ||
                !append_indent(source, depth + 2) ||
                !buffer_appendf(source, "%s %s = nyx_for_values_%zu["
                                        "nyx_for_i_%zu];\n",
                                value_c, value_name, offset, offset) ||
                !append_indent(source, depth + 2) ||
                !buffer_appendf(source, "(void)%s;\n", value_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    }
    if (!emit_block(program, node->right_child, depth + 2, effective_source,
                    source, errors) ||
        !append_indent(source, depth + 1) ||
        !buffer_append(source, "}\n") ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


bool emit_block(const TypedProgram *program, size_t block_index,
                       size_t depth, const char *effective_source,
                       TextBuffer *source, ErrorState *errors)
{
    const TypedNode *block = typed_program_node(program, block_index);
    size_t i;

    if (block == NULL || block->kind != TYPED_BLOCK) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    for (i = 0; i < block->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 block->children_start + i);

        if (!emit_statement(program, child, depth, effective_source, source,
                            errors)) {
            return false;
        }
    }
    return true;
}


bool emit_statement(const TypedProgram *program, size_t node_index,
                           size_t depth, const char *effective_source,
                           TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);

    if (node == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }

    if (node->kind == TYPED_RETURN) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "return ") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_BREAK || node->kind == TYPED_CONTINUE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source,
                           node->kind == TYPED_BREAK ? "break;\n"
                                                     : "continue;\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_IF) {
        size_t then_block;
        size_t else_block = TYPED_NO_NODE;

        if (node->children_count == 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        then_block = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        if (node->children_count > 1) {
            else_block = *(const size_t *)list_get(&program->children,
                                                   node->children_start + 1);
        }
        if (!append_indent(source, depth) ||
            !buffer_append(source, "if (") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ") {\n") ||
            !emit_block(program, then_block, depth + 1, effective_source,
                        source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (else_block != TYPED_NO_NODE) {
            if (!append_indent(source, depth) ||
                !buffer_append(source, "} else {\n") ||
                !emit_block(program, else_block, depth + 1, effective_source,
                            source, errors)) {
                if (!errors->reported) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                }
                return false;
            }
        }
        if (!append_indent(source, depth) || !buffer_append(source, "}\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_WHILE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "while (") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ") {\n") ||
            !emit_block(program, node->right_child, depth + 1,
                        effective_source, source, errors) ||
            !append_indent(source, depth) ||
            !buffer_append(source, "}\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_FOR) {
        const TypedNode *iterable_node = typed_program_node(
            program, node->left_child);
        const TypedType *iterable_type =
            iterable_node == NULL
                ? NULL
                : typed_program_type(program, iterable_node->type_index);
        char value_name_text[USER_NAME_CAPACITY];
    const char *value_name = user_c_name(program, node->name_index,
                                  value_name_text, sizeof(value_name_text));
        char index_name_text[USER_NAME_CAPACITY];
        const char *index_name =
            node->second_name_index == TYPED_NO_NODE
                ? NULL
                : user_c_name(program, node->second_name_index,
                              index_name_text, sizeof(index_name_text));
        char iterable_name[NIPPY_IDENTIFIER_MAX + 32];
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        size_t offset = node->pos.offset;
        const char *value_c;
        const char *index_c;
        bool check_length;

        if (iterable_type != NULL &&
            (iterable_type->kind == NIPPY_TYPE_RECORD ||
             iterable_type->kind == NIPPY_TYPE_TABLE)) {
            return emit_map_for(program, node, iterable_type, depth,
                                effective_source, source, errors);
        }
        if (iterable_type == NULL || value_name == NULL ||
            !valid_identifier(value_name) ||
            (index_name != NULL && !valid_identifier(index_name)) ||
            !format_c_type(program, iterable_node->type_index, iterable_name,
                           sizeof(iterable_name), errors) ||
            !format_c_type(program, node->auxiliary_type, element_name,
                           sizeof(element_name), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        value_c = value_name;
        index_c = index_name;
        check_length = iterable_type->kind == NIPPY_TYPE_LIST ||
                       iterable_type->kind == NIPPY_TYPE_FIXED_LIST;

        if (!append_indent(source, depth) ||
            !buffer_append(source, "{\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nyx_for_src_%zu = ",
                            iterable_name, offset) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source, "size_t nyx_for_len_%zu = (size_t)(",
                            offset)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (iterable_type->kind == NIPPY_TYPE_LIST) {
            if (!buffer_appendf(source, "nyx_list_len(nyx_for_src_%zu)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_FIXED_LIST ||
                   iterable_type->kind == NIPPY_TYPE_STRING) {
            if (!buffer_appendf(source,
                                "((int32_t)(nyx_for_src_%zu).length)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_SLICE) {
            if (!buffer_appendf(source, "nyx_slice_len(nyx_for_src_%zu)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_appendf(source, "nyx_string_len(nyx_for_src_%zu)",
                                offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        if (!buffer_append(source, ");\n") ||
            !append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "for (size_t nyx_for_i_%zu = 0; "
                            "nyx_for_i_%zu < nyx_for_len_%zu; "
                            "nyx_for_i_%zu++) {\n",
                            offset, offset, offset, offset) ||
            !append_indent(source, depth + 2)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (check_length) {
            if (iterable_type->kind == NIPPY_TYPE_LIST) {
                if (!buffer_appendf(source,
                                    "if ((size_t)nyx_list_len("
                                    "nyx_for_src_%zu) != "
                                    "nyx_for_len_%zu) { fprintf(stderr, \"%s\\n\"); "
                                    "exit(1); }\n",
                                    offset, offset,
                                    MESSAGE_RUNTIME_FOR_MUTATED)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            } else if (!buffer_appendf(source,
                                       "if ((size_t)(nyx_for_src_%zu).length "
                                       "!= nyx_for_len_%zu) { "
                                       "fprintf(stderr, \"%s\\n\"); "
                                       "exit(1); }\n",
                                       offset, offset,
                                       MESSAGE_RUNTIME_FOR_MUTATED)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!append_indent(source, depth + 2)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(source, "%s %s = ", element_name, value_c)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (iterable_type->kind == NIPPY_TYPE_FIXED_LIST) {
            if (!buffer_appendf(source, "nyx_list_%zu_get(nyx_for_src_%zu, "
                                        "nyx_for_i_%zu)",
                                iterable_node->type_index, offset, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_LIST) {
            if (!buffer_appendf(source,
                                "(*(%s *)nyx_list_data(nyx_for_src_%zu, "
                                "nyx_for_i_%zu, sizeof(%s)))",
                                element_name, offset, offset, element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_SLICE) {
            if (!buffer_appendf(source,
                                "(*(%s *)nyx_slice_data(nyx_for_src_%zu, "
                                "nyx_for_i_%zu, sizeof(%s)))",
                                element_name, offset, offset, element_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_STRING) {
            if (!buffer_appendf(source, "nyx_string_%zu_at(nyx_for_src_%zu, "
                                        "nyx_for_i_%zu)",
                                iterable_type->fixed_length, offset, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else if (iterable_type->kind == NIPPY_TYPE_DYNAMIC_STRING) {
            if (!buffer_appendf(source, "nyx_string_byte_at(nyx_for_src_%zu, "
                                        "nyx_for_i_%zu)",
                                offset, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        } else {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        if (!buffer_append(source, ";\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (index_c != NULL) {
            if (!append_indent(source, depth + 2) ||
                !buffer_appendf(source, "int32_t %s = (int32_t)nyx_for_i_%zu;\n",
                                index_c, offset)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!emit_block(program, node->right_child, depth + 2,
                        effective_source, source, errors)) {
            return false;
        }
        if (check_length) {
            if (!append_indent(source, depth + 2)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (iterable_type->kind == NIPPY_TYPE_LIST) {
                if (!buffer_appendf(source,
                                    "if ((size_t)nyx_list_len("
                                    "nyx_for_src_%zu) != "
                                    "nyx_for_len_%zu) { fprintf(stderr, \"%s\\n\"); "
                                    "exit(1); }\n",
                                    offset, offset,
                                    MESSAGE_RUNTIME_FOR_MUTATED)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            } else if (!buffer_appendf(source,
                                       "if ((size_t)(nyx_for_src_%zu).length "
                                       "!= nyx_for_len_%zu) { "
                                       "fprintf(stderr, \"%s\\n\"); "
                                       "exit(1); }\n",
                                       offset, offset,
                                       MESSAGE_RUNTIME_FOR_MUTATED)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_append(source, "}\n") ||
            !append_indent(source, depth) ||
            !buffer_append(source, "}\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_TABLE_SET) {
        size_t key = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        size_t value = *(const size_t *)list_get(&program->children,
                                                 node->children_start + 1);
        const TypedNode *receiver = typed_program_node(
            program, node->left_child);

        if (receiver == NULL || !append_indent(source, depth) ||
            !buffer_appendf(source, "nyx_table_%zu_set(&(",
                            receiver->type_index) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, "), ") ||
            !emit_expression(program, key, source, errors) ||
            !buffer_append(source, ", ") ||
            !emit_expression(program, value, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_RECORD_SET) {
        size_t key = *(const size_t *)list_get(&program->children,
                                               node->children_start);
        size_t value = *(const size_t *)list_get(&program->children,
                                                 node->children_start + 1);
        const TypedNode *receiver = typed_program_node(
            program, node->left_child);

        if (receiver == NULL || !append_indent(source, depth) ||
            !buffer_append(source, "nyx_record_") ||
            !buffer_appendf(source, "%zu", receiver->type_index) ||
            !buffer_append(source, "_set(&(") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, "), ") ||
            !emit_expression(program, key, source, errors) ||
            !buffer_append(source, ", ") ||
            !emit_expression(program, value, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_LOCAL_DECLARATION) {
        char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));
        char type_name[NIPPY_IDENTIFIER_MAX + 32];

        if (!valid_identifier(name) ||
            !format_c_type(program, node->type_index, type_name,
                           sizeof(type_name), errors) ||
            node->left_child == TYPED_NO_NODE) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
            return false;
        }
        if (!append_indent(source, depth) ||
            !buffer_appendf(source, "%s %s = ", type_name, name) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_MEMBER_WRITE) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "(void)(") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, " = ") ||
            !emit_expression(program, node->right_child, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_ASSIGNMENT) {
        char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));

        if (!valid_identifier(name)) {
            error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
            return false;
        }
        if (!append_indent(source, depth) ||
            !buffer_appendf(source, "%s = ", name) ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_LIST_SET) {
        if (!append_indent(source, depth) ||
            !buffer_append(source, "(void)(") ||
            !emit_expression(program, node_index, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_PRINT) {
        return emit_print_statement(program, node, source, errors, depth);
    }

    if (node->kind == TYPED_EXPRESSION_STATEMENT) {
        const TypedNode *expression = typed_program_node(
            program, node->left_child);

        if (expression != NULL && expression->kind == TYPED_PRINT) {
            return emit_print_statement(program, expression, source, errors,
                                        depth);
        }
        /* The outer cast discards the expression's value so a store reset or
           write statement is warning-free under -Wall. */
        if (!append_indent(source, depth) ||
            !buffer_append(source, "(void)(") ||
            !emit_expression(program, node->left_child, source, errors) ||
            !buffer_append(source, ");\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }

    if (node->kind == TYPED_BLOCK) {
        return emit_block(program, node_index, depth, effective_source,
                          source, errors);
    }

    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
    return false;
}


static bool emit_function_signature(const TypedProgram *program,
                                    const TypedNode *node, const char *name,
                                    TextBuffer *source, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature;
    char return_name[NIPPY_IDENTIFIER_MAX + 32];
    size_t i;

    if (!valid_identifier(name) || type == NULL ||
        type->kind != NIPPY_TYPE_FUNCTION) {
        error_report(errors, valid_identifier(name)
                                 ? MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION
                                 : MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    if (signature == NULL ||
        !format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        }
        return false;
    }
    if (!buffer_appendf(source, "%s %s(", return_name, name)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (node->children_count == 0) {
        if (!buffer_append(source, "void")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    } else {
        for (i = 0; i < node->children_count; i++) {
            size_t child = *(const size_t *)list_get(&program->children,
                                                     node->children_start + i);
            const TypedNode *parameter = typed_program_node(program, child);
            char parameter_text[USER_NAME_CAPACITY];
            const char *parameter_name;
            char parameter_type[NIPPY_IDENTIFIER_MAX + 32];

            if (parameter == NULL || parameter->kind != TYPED_PARAMETER ||
                !format_c_type(program, parameter->type_index, parameter_type,
                               sizeof(parameter_type), errors)) {
                if (!errors->reported) {
                    error_report(errors,
                                 MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
                }
                return false;
            }
            parameter_name = user_c_name(program, parameter->name_index,
                                         parameter_text,
                                         sizeof(parameter_text));
            if (!valid_identifier(parameter_name)) {
                error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
                return false;
            }
            if (i > 0 && !buffer_append(source, ", ")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!buffer_appendf(source, "%s %s", parameter_type,
                                parameter_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
    }
    return buffer_append(source, ")");
}


static bool emit_function_call_args(const TypedProgram *program,
                                    const TypedNode *node, TextBuffer *source,
                                    ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *parameter = typed_program_node(program, child);
        char parameter_text[USER_NAME_CAPACITY];
        const char *parameter_name;

        if (parameter == NULL || parameter->kind != TYPED_PARAMETER ||
            (parameter_name = user_c_name(program, parameter->name_index,
                                          parameter_text,
                                          sizeof(parameter_text))) == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
            return false;
        }
        if (i > 0 && !buffer_append(source, ", ")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_append(source, parameter_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


static bool emit_function_unused_params(const TypedProgram *program,
                                        const TypedNode *node,
                                        TextBuffer *source,
                                        ErrorState *errors)
{
    size_t i;

    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(&program->children,
                                                 node->children_start + i);
        const TypedNode *parameter = typed_program_node(program, child);
        char parameter_name_text[USER_NAME_CAPACITY];
    const char *parameter_name = user_c_name(program, parameter->name_index,
                                  parameter_name_text, sizeof(parameter_name_text));

        if (parameter_name == NULL ||
            !buffer_appendf(source, "    (void)%s;\n", parameter_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


bool emit_function(const TypedProgram *program, const TypedNode *node,
                          const char *effective_source, TextBuffer *source,
                          ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));
    const TypedType *type = typed_program_type(program, node->type_index);
    const TypedFunctionSignature *signature;
    bool recursive;
    char inner_name[NIPPY_IDENTIFIER_MAX + 32];
    char depth_name[NIPPY_IDENTIFIER_MAX + 32];
    char return_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!valid_identifier(name) || type == NULL ||
        type->kind != NIPPY_TYPE_FUNCTION) {
        error_report(errors, valid_identifier(name)
                                 ? MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION
                                 : MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }
    signature = typed_program_signature(program, type->signature_index);
    recursive = typed_calls_name(program, node->left_child,
                                 node->name_index);

    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    if (!recursive) {
        if (!emit_function_signature(program, node, name, source, errors) ||
            !buffer_append(source, "\n{\n") ||
            !emit_function_unused_params(program, node, source, errors) ||
            !emit_block(program, node->left_child, 1, effective_source,
                        source, errors) ||
            !buffer_append(source, "}\n\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        return true;
    }
    if (signature == NULL ||
        !format_c_type(program, signature->return_type, return_name,
                       sizeof(return_name), errors)) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION);
        return false;
    }
    snprintf(inner_name, sizeof(inner_name), "nyx_inner_%s", name);
    snprintf(depth_name, sizeof(depth_name), "nyx_depth_%s", name);
    if (!buffer_appendf(source, "static size_t %s = 0;\n\n",
                        depth_name) ||
        !emit_function_signature(program, node, inner_name, source, errors) ||
        !buffer_append(source, ";\n\n") ||
        !emit_function_signature(program, node, name, source, errors) ||
        !buffer_append(source, "\n{\n") ||
        !buffer_appendf(source,
                        "    if (++%s > NIPPY_RECURSION_LIMIT) {\n"
                        "        fprintf(stderr, \"" MESSAGE_RUNTIME_RECURSION
                        "\\n\");\n"
                        "        exit(1);\n"
                        "    }\n"
                        "    %s nyx_recursion_result = %s(",
                        depth_name, return_name, inner_name) ||
        !emit_function_call_args(program, node, source, errors) ||
        !buffer_appendf(source,
                        ");\n"
                        "    %s--;\n"
                        "    return nyx_recursion_result;\n"
                        "}\n\n",
                        depth_name) ||
        !emit_function_signature(program, node, inner_name, source, errors) ||
        !buffer_append(source, "\n{\n") ||
        !emit_function_unused_params(program, node, source, errors) ||
        !emit_block(program, node->left_child, 1, effective_source, source,
                    errors) ||
        !buffer_append(source, "}\n\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}
