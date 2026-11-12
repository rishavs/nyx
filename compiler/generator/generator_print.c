#include "generator_internal.h"


bool print_type_in_list(const List *types, size_t type_index)
{
    size_t i;

    for (i = 0; i < types->length; i++) {
        if (*(const size_t *)list_get(types, i) == type_index) {
            return true;
        }
    }
    return false;
}


bool collect_print_type(const TypedProgram *program, size_t type_index,
                               List *types, unsigned depth, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    size_t i;

    if (type == NULL || depth == 0) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (print_type_in_list(types, type_index)) {
        return true;
    }
    if (!list_push(types, &type_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    switch (type->kind) {
    case NIPPY_TYPE_UNION:
        for (i = 0; i < type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!collect_print_type(program, alternative, types,
                                    depth - 1, errors)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);

        if (declaration == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);

            if (field == NULL ||
                !collect_print_type(program, field->type_index, types,
                                    depth - 1, errors)) {
                return false;
            }
        }
        return true;
    }
    case NIPPY_TYPE_ERROR:
        for (i = 0; i < type->child_count; i++) {
            size_t field = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!collect_print_type(program, field, types, depth - 1,
                                    errors)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_RECORD:
    case NIPPY_TYPE_TABLE:
        if (type->child_count != 2) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        for (i = 0; i < 2; i++) {
            size_t part = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!collect_print_type(program, part, types, depth - 1,
                                    errors)) {
                return false;
            }
        }
        return true;
    case NIPPY_TYPE_LIST:
    case NIPPY_TYPE_FIXED_LIST:
    case NIPPY_TYPE_SLICE:
        if (type->child_count != 1) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        return collect_print_type(
            program, *(const size_t *)list_get(&program->type_children,
                                               type->first_child),
            types, depth - 1, errors);
    case NIPPY_TYPE_STORE:
    case NIPPY_TYPE_UNKNOWN:
    case NIPPY_TYPE_STRING_LITERAL:
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    default:
        return true;
    }
}


bool emit_print_helper(TextBuffer *source, const TypedProgram *program,
                              size_t type_index, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    char c_type[NIPPY_IDENTIFIER_MAX + 32];
    size_t i;

    if (type == NULL ||
        !format_c_type(program, type_index, c_type, sizeof(c_type), errors)) {
        return false;
    }
    if (!buffer_appendf(source,
                        "static void nyx_print_%zu(%s value, int depth)\n"
                        "{\n"
                        "    (void)value;\n"
                        "    if (depth > %d) {\n"
                        "        fputs(\"<...>\", stdout);\n"
                        "        return;\n"
                        "    }\n",
                        type_index, c_type, program->max_print_depth)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    switch (type->kind) {
    case NIPPY_TYPE_INT32:
        if (!buffer_append(source,
                           "    fprintf(stdout, \"%d\", (int)value);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_BYTE:
        if (!buffer_append(source,
                           "    fprintf(stdout, \"%u\", (unsigned)value);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_BOOL:
        if (!buffer_append(source,
                           "    fputs(value ? \"true\" : \"false\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_NIL:
        if (!buffer_append(source, "    fputs(\"nil\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_FUNCTION:
        if (!buffer_append(source, "    fputs(\"<function>\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_DYNAMIC_STRING:
        if (!buffer_append(source,
                           "    if (value.length != 0) {\n"
                           "        fwrite(nyx_string_data(value), 1, "
                           "value.length, stdout);\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_STRING:
        if (!buffer_append(source,
                           "    if (value.length != 0) {\n"
                           "        fwrite(value.data, 1, value.length, "
                           "stdout);\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_TEXT_BUILDER:
        if (!buffer_append(source,
                           "    nyx_builder_state *state = "
                           "nyx_builder_get_state(value);\n"
                           "    nyx_store *store = nyx_builder_store(value);\n"
                           "    if (state->length != 0) {\n"
                           "        fwrite(store->data + state->offset, 1, "
                           "state->length, stdout);\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_HANDLE:
        if (!buffer_append(source,
                           "    fprintf(stdout, \"<handle store=%u offset=%zu "
                           "generation=%llu>\", value.store, value.offset, "
                           "(unsigned long long)value.generation);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_STRUCT: {
        const TypedNode *declaration = typed_program_node(
            program, type->declaration_node);
        const char *name = type->source_name_index == TYPED_NO_NODE
                               ? "struct"
                               : typed_program_symbol_text(
                                     program, type->source_name_index);

        if (declaration == NULL || name == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
            return false;
        }
        if (!buffer_appendf(source, "    fputs(\"%s { \", stdout);\n", name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < declaration->children_count; i++) {
            size_t child = *(const size_t *)list_get(
                &program->children, declaration->children_start + i);
            const TypedNode *field = typed_program_node(program, child);
            const char *field_name =
                field == NULL
                    ? NULL
                    : typed_program_symbol_text(program, field->name_index);

            if (field == NULL || field_name == NULL) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            if (!buffer_appendf(source, "    fputs(\"%s%s: \", stdout);\n",
                                i == 0 ? "" : ", ", field_name) ||
                !buffer_appendf(source,
                                "    nyx_print_%zu(value.%s, depth + 1);\n",
                                field->type_index, field_name)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(source, "    fputs(\" }\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_UNION:
        if (!buffer_append(source, "    switch (value.tag) {\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < type->child_count; i++) {
            size_t alternative = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);
            const TypedType *alternative_type = typed_program_type(
                program, alternative);

            if (alternative_type == NULL) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
                return false;
            }
            if (!buffer_appendf(source, "    case %zu:\n", i)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (alternative_type->kind == NIPPY_TYPE_NIL) {
                if (!buffer_append(source,
                                   "        fputs(\"nil\", stdout);\n")) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    return false;
                }
            } else if (!buffer_appendf(
                           source,
                           "        nyx_print_%zu(value.value.v%zu, "
                           "depth + 1);\n",
                           alternative, i)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            if (!buffer_append(source, "        break;\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(source,
                           "    default:\n"
                           "        fputs(\"<invalid union>\", stdout);\n"
                           "        break;\n"
                           "    }\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    case NIPPY_TYPE_ERROR: {
        static const char *const names[] = {
            "header", "description", "filename", "line", "col", "pos"
        };

        if (!buffer_append(source, "    fputs(\"Error(\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (i = 0; i < type->child_count && i < 6; i++) {
            size_t field = *(const size_t *)list_get(
                &program->type_children, type->first_child + i);

            if (!buffer_appendf(source, "    fputs(\"%s\", stdout);\n",
                                i == 0 ? "" : ", ") ||
                !buffer_appendf(source,
                                "    nyx_print_%zu(value.%s, depth + 1);\n",
                                field, names[i])) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_appendf(source,
                            "    fprintf(stdout, \", %%d, %%d, %%d\", "
                            "(int)value.line, (int)value.col, (int)value.pos);\n"
                            "    fputs(\")\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_FIXED_LIST: {
        size_t element = *(const size_t *)list_get(&program->type_children,
                                                   type->first_child);

        if (!buffer_append(source,
                           "    fputs(\"[\", stdout);\n"
                           "    for (size_t i = 0; i < value.length; i++) {\n"
                           "        if (i != 0) fputs(\", \", stdout);\n") ||
            !buffer_appendf(source,
                            "        nyx_print_%zu(value.data[i], "
                            "depth + 1);\n",
                            element) ||
            !buffer_append(source,
                           "    }\n"
                           "    fputs(\"]\", stdout);\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_LIST:
    case NIPPY_TYPE_SLICE: {
        size_t element = *(const size_t *)list_get(&program->type_children,
                                                   type->first_child);
        char element_name[NIPPY_IDENTIFIER_MAX + 32];
        const char *get = type->kind == NIPPY_TYPE_LIST
                              ? "nyx_list_len(value)"
                              : "nyx_slice_len(value)";
        const char *data = type->kind == NIPPY_TYPE_LIST
                               ? "nyx_list_data"
                               : "nyx_slice_data";

        if (!format_c_type(program, element, element_name,
                           sizeof(element_name), errors)) {
            return false;
        }
        if (!buffer_appendf(source,
                            "    fputs(\"[\", stdout);\n"
                            "    for (size_t i = 0; i < (size_t)%s; i++) {\n"
                            "        if (i != 0) fputs(\", \", stdout);\n"
                            "        nyx_print_%zu(*(%s *)%s(value, i, "
                            "sizeof(%s)), depth + 1);\n"
                            "    }\n"
                            "    fputs(\"]\", stdout);\n",
                            get, element, element_name, data, element_name)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_RECORD: {
        size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                    type->first_child);
        size_t value_type = *(const size_t *)list_get(
            &program->type_children, type->first_child + 1);
        size_t buckets = type->fixed_length == 0 ? 1 : type->fixed_length + 1;

        if (!buffer_append(source,
                           "    fputs(\"{\", stdout);\n"
                           "    size_t count = 0;\n"
                           "    for (size_t i = 0; i < ") ||
            !buffer_appendf(source, "%zu", buckets) ||
            !buffer_append(source,
                           "; i++) {\n"
                           "        if (value.states[i] != 1) continue;\n"
                           "        if (count++ != 0) fputs(\", \", stdout);\n") ||
            !buffer_appendf(source, "        nyx_print_%zu(value.keys[i], "
                                    "depth + 1);\n"
                                    "        fputs(\": \", stdout);\n"
                                    "        nyx_print_%zu(value.values[i], "
                                    "depth + 1);\n"
                                    "    }\n"
                                    "    fputs(\"}\", stdout);\n",
                            key_type, value_type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    case NIPPY_TYPE_TABLE: {
        size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                    type->first_child);
        size_t value_type = *(const size_t *)list_get(
            &program->type_children, type->first_child + 1);
        char key_name[64];
        char value_name[64];

        if (!format_c_type(program, key_type, key_name, sizeof(key_name),
                           errors) ||
            !format_c_type(program, value_type, value_name,
                           sizeof(value_name), errors)) {
            return false;
        }
        if (!buffer_appendf(source,
            "    nyx_table_state_%zu *state = nyx_table_%zu_state(value);\n"
            "    nyx_store *store = &nyx_stores[value.store];\n"
            "    %s *keys = (%s *)(void *)(store->data + state->keys_offset);\n"
            "    %s *values = (%s *)(void *)(store->data + state->values_offset);\n"
            "    uint8_t *states = (uint8_t *)(void *)(store->data + "
            "state->states_offset);\n"
            "    fputs(\"{\", stdout);\n"
            "    size_t count = 0;\n"
            "    for (size_t i = 0; i < state->buckets; i++) {\n"
            "        if (states[i] != 1) continue;\n"
            "        if (count++ != 0) fputs(\", \", stdout);\n"
            "        nyx_print_%zu(keys[i], depth + 1);\n"
            "        fputs(\": \", stdout);\n"
            "        nyx_print_%zu(values[i], depth + 1);\n"
            "    }\n"
            "    fputs(\"}\", stdout);\n",
            type_index, type_index, key_name, key_name, value_name,
            value_name, key_type, value_type)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        break;
    }
    default:
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    if (!buffer_append(source, "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


bool emit_print_helpers(const TypedProgram *program, TextBuffer *source,
                               ErrorState *errors)
{
    List types;
    size_t i;
    bool ok = true;

    list_init(&types, sizeof(size_t));
    for (i = 0; i < program->nodes.length && ok; i++) {
        const TypedNode *node = typed_program_node(program, i);

        if (node == NULL || node->kind != TYPED_PRINT) {
            continue;
        }
        for (size_t j = 0; j < node->children_count; j++) {
            size_t child = *(const size_t *)list_get(
                &program->children, node->children_start + j);
            const TypedNode *argument = typed_program_node(program, child);

            if (argument == NULL ||
                !collect_print_type(program, argument->type_index, &types,
                                    32, errors)) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        for (i = 0; i < types.length; i++) {
            size_t type_index = *(const size_t *)list_get(&types, i);
            char c_type[NIPPY_IDENTIFIER_MAX + 32];

            if (!format_c_type(program, type_index, c_type,
                               sizeof(c_type), errors) ||
                !buffer_appendf(source,
                                "static void nyx_print_%zu(%s value, int "
                                "depth);\n",
                                type_index, c_type)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                ok = false;
                break;
            }
        }
    }
    for (i = 0; ok && i < types.length; i++) {
        size_t type_index = *(const size_t *)list_get(&types, i);

        if (!emit_print_helper(source, program, type_index, errors)) {
            ok = false;
        }
    }
    if (ok && types.length > 0 &&
        !buffer_append(source, "\n")) {
        ok = false;
    }
    list_free(&types);
    return ok;
}


bool emit_print_statement(const TypedProgram *program,
                                 const TypedNode *node, TextBuffer *source,
                                 ErrorState *errors, size_t depth)
{
    size_t i;

    if (!append_indent(source, depth) ||
        !buffer_append(source, "{\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    for (i = 0; i < node->children_count; i++) {
        size_t child = *(const size_t *)list_get(
            &program->children, node->children_start + i);
        const TypedNode *argument = typed_program_node(program, child);
        char c_type[NIPPY_IDENTIFIER_MAX + 32];

        if (argument == NULL ||
            !format_c_type(program, argument->type_index, c_type,
                           sizeof(c_type), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_appendf(source, "%s nyx_print_arg_%zu_%zu = ",
                            c_type, node->pos.offset, i) ||
            !emit_expression(program, child, source, errors) ||
            !buffer_append(source, ";\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return false;
        }
        if (i != 0) {
            if (!append_indent(source, depth + 1) ||
                !buffer_append(source, "fputc(' ', stdout);\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!append_indent(source, depth + 1) ||
            !buffer_appendf(source,
                            "nyx_print_%zu(nyx_print_arg_%zu_%zu, 0);\n",
                            argument->type_index, node->pos.offset, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    if (!append_indent(source, depth + 1) ||
        !buffer_append(source, "fputc('\\n', stdout);\n") ||
        !append_indent(source, depth) ||
        !buffer_append(source, "}\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
