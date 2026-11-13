#include "generator_internal.h"


bool generator_generate(const TypedProgram *program, const char *header_name,
                        const char *source_name, char **c_source,
                        char **h_source, ErrorState *errors)
{
    const char *effective_header = header_name == NULL ? "nippy.h" : header_name;
    const char *effective_source = source_name == NULL ? "nippy.nip" : source_name;
    TextBuffer header;
    TextBuffer source;
    size_t var_count = 0;
    const TypedNode *single_var = NULL;
    bool print_program = false;
    bool has_expression_statement = false;
    bool uses_store_or_handle;
    bool uses_dynamic_string;
    bool uses_string_builder;
    bool uses_list;
    bool uses_dynamic_list;
    bool uses_slice;
    bool uses_print;
    bool uses_recursion;
    bool uses_conversion;
    bool uses_map;
    bool uses_table;
    bool uses_arith_helpers;
    size_t store_count;
    size_t i;
    bool ok = false;

    *c_source = NULL;
    *h_source = NULL;

    if (!valid_header_name(effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME);
        return false;
    }
    if (!valid_line_name(effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_LINE_NAME);
        return false;
    }

    buffer_init(&header);
    buffer_init(&source);

    store_count = count_store_declarations(program);
    if (!validate_store_declarations(program, store_count, errors)) {
        goto done;
    }
    uses_store_or_handle =
        store_count > 0 || program_uses_handle_or_store_type(program);
    uses_string_builder = program_uses_string_builder(program);
    uses_dynamic_string =
        program_uses_dynamic_string(program) || uses_string_builder;
    uses_list = program_uses_list(program);
    uses_dynamic_list = program_uses_dynamic_list(program);
    uses_slice = program_uses_slice(program);
    uses_print = program_uses_print(program);
    uses_recursion = program_uses_recursion(program);
    uses_conversion = program_uses_numeric_conversion(program);
    uses_arith_helpers =
        program_uses_binary_operator(program, NIPPY_OPERATOR_DIV) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_MOD) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_SHL) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_SHR);
    uses_map = false;
    uses_table = false;
    {
        size_t type_i;

        for (type_i = 0; type_i < program->types.length; type_i++) {
            const TypedType *type = typed_program_type(program, type_i);

            if (type != NULL && type->kind == NIPPY_TYPE_RECORD) {
                uses_map = true;
            }
            if (type != NULL && type->kind == NIPPY_TYPE_TABLE) {
                uses_map = true;
                uses_table = true;
            }
        }
    }

    if (!buffer_append(&header,
                       "#ifndef NIPPY_GENERATED_H\n"
                       "#define NIPPY_GENERATED_H\n\n"
                       "#include <stdbool.h>\n"
                       "#include <stdint.h>\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_store_or_handle || uses_dynamic_string || uses_list ||
        uses_slice) {
        if (!buffer_append(&header, "#include <stddef.h>\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }
    if (!buffer_append(&header, "\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_map && !buffer_append(&header, "#include <stddef.h>\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    if (uses_store_or_handle) {
        if (!buffer_append(
                &header,
                "typedef struct {\n"
                "    uint32_t store;\n"
                "    size_t offset;\n"
                "    uint64_t generation;\n"
                "} nyx_handle;\n\n"
                "typedef struct {\n"
                "    unsigned char *data;\n"
                "    size_t used;\n"
                "    size_t capacity;\n"
                "    uint64_t generation;\n"
                "} nyx_store;\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }

    if (!emit_type_definitions(program, effective_source, &header, errors)) {
        goto done;
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node == NULL) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
        if (node->kind == TYPED_TYPE_DECLARATION) {
            /* Struct and union definitions come from program->types so that
               instantiations that have no root are emitted too. */
            continue;
        } else if (node->kind == TYPED_STORE_DECLARATION) {
            /* Store roots become table entries and main() init calls, never
               C locals. */
            continue;
        } else if (node->kind == TYPED_DECLARATION) {
            var_count++;
            single_var = node;
        } else if (node->kind == TYPED_EXPRESSION_STATEMENT) {
            has_expression_statement = true;
        } else if (node->kind == TYPED_ASSIGNMENT ||
                   node->kind == TYPED_LIST_SET ||
                   node->kind == TYPED_MEMBER_WRITE ||
                   node->kind == TYPED_RECORD_SET ||
                   node->kind == TYPED_TABLE_SET ||
                   node->kind == TYPED_FOR ||
                   node->kind == TYPED_WHILE ||
                   node->kind == TYPED_IF) {
            /* An assignment changes the printed value instead of replacing the
               program-value entry point. */
        } else if (node->kind != TYPED_FUNCTION_DECLARATION) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
    }

    if (!emit_function_typedefs(program, &header, errors)) {
        goto done;
    }

    if (!has_expression_statement && var_count == 1 &&
        typed_program_type(program, single_var->type_index) != NULL &&
        typed_program_type(program, single_var->type_index)->kind ==
            NIPPY_TYPE_INT32) {
        print_program = true;
    }

    if (print_program &&
        !buffer_append(&header, "int32_t nyx_program_value(void);\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (!buffer_append(&header, "#endif\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }

    if (!buffer_appendf(&source, "#include \"%s\"\n\n", effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (store_count > 0 || program_uses_string(program) ||
        uses_dynamic_string || uses_list || uses_slice || uses_print ||
        uses_map || uses_arith_helpers || uses_recursion ||
        uses_conversion) {
        if (!buffer_append(&source,
                           "#include <stdio.h>\n"
                           "#include <stdlib.h>\n"
                           "#include <string.h>\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
        if (uses_conversion &&
            !buffer_append(&source, "#include <float.h>\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    } else if (print_program &&
               !buffer_append(&source, "#include <stdio.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_recursion &&
        !buffer_append(&source, "#define NIPPY_RECURSION_LIMIT 10000\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        goto done;
    }
    if (uses_dynamic_string || uses_list || uses_slice || uses_arith_helpers ||
        uses_conversion) {
        if (!buffer_append(&source,
                           "#if defined(__GNUC__) || defined(__clang__)\n"
                           "#define NIPPY_UNUSED __attribute__((unused))\n"
                           "#else\n"
                           "#define NIPPY_UNUSED\n"
                           "#endif\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            goto done;
        }
    }
    if ((uses_arith_helpers || uses_conversion) &&
        !emit_arithmetic_runtime(&source, errors)) {
        goto done;
    }
    if (uses_conversion && !emit_conversion_runtime(&source, errors)) {
        goto done;
    }
    if (store_count > 0 &&
        !emit_store_runtime(&source, store_count, uses_dynamic_string,
                            uses_dynamic_list || uses_table, errors)) {
        goto done;
    }
    if (program_uses_string(program) &&
        !emit_string_runtime(program, &source, errors)) {
        goto done;
    }
    if (uses_dynamic_string &&
        !emit_dynamic_string_runtime(program, store_count, &source, errors)) {
        goto done;
    }
    if (uses_dynamic_string &&
        !emit_dynamic_string_literals(program, &source, errors)) {
        goto done;
    }
    if (uses_string_builder && store_count > 0 &&
        !emit_builder_runtime(&source, errors)) {
        goto done;
    }
    if (uses_list && !emit_list_runtime(program, store_count, &source,
                                        errors)) {
        goto done;
    }
    if (uses_slice && uses_dynamic_list && store_count > 0 &&
        !emit_slice_runtime(program, &source, errors)) {
        goto done;
    }
    if (uses_map && !emit_map_runtime(program, store_count, &source, errors)) {
        goto done;
    }
    if (uses_print && !emit_print_helpers(program, &source, errors)) {
        goto done;
    }

    for (i = 0; i < program->roots.length; i++) {
        size_t root = *(const size_t *)list_get(&program->roots, i);
        const TypedNode *node = typed_program_node(program, root);

        if (node->kind == TYPED_FUNCTION_DECLARATION &&
            !emit_function(program, node, effective_source, &source, errors)) {
            goto done;
        }
    }

    if (print_program) {
        if (!append_print_program(program, single_var, store_count,
                                  effective_source, &source, errors)) {
            goto done;
        }
    } else {
        if (!buffer_append(&source, "int main(void)\n{\n") ||
            !emit_store_inits(program, store_count, &source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            goto done;
        }
        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node->kind == TYPED_DECLARATION &&
                !emit_var_to_main(program, node, effective_source, &source,
                                  errors)) {
                goto done;
            }
            if ((node->kind == TYPED_EXPRESSION_STATEMENT ||
                 node->kind == TYPED_ASSIGNMENT ||
                 node->kind == TYPED_LIST_SET ||
                 node->kind == TYPED_MEMBER_WRITE ||
                 node->kind == TYPED_RECORD_SET ||
                 node->kind == TYPED_TABLE_SET ||
                 node->kind == TYPED_FOR ||
                 node->kind == TYPED_WHILE ||
                 node->kind == TYPED_IF) &&
                !emit_statement(program, root, 1, effective_source, &source,
                                errors)) {
                goto done;
            }
        }
        if (!emit_store_frees(store_count, &source, errors) ||
            !buffer_append(&source, "    return 0;\n}\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            goto done;
        }
    }

    *h_source = header.text;
    header.text = NULL;
    *c_source = source.text;
    source.text = NULL;
    ok = true;

done:
    buffer_free(&header);
    buffer_free(&source);
    return ok;
}


bool emit_var_to_main(const TypedProgram *program, const TypedNode *node,
                             const char *effective_source, TextBuffer *source,
                             ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, node->name_index,
                                  name_text, sizeof(name_text));
    char type_name[NIPPY_IDENTIFIER_MAX + 32];

    if (!valid_identifier(name) ||
        !format_c_type(program, node->type_index, type_name,
                       sizeof(type_name), errors)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
        }
        return false;
    }
    if (!buffer_appendf(source, "#line %zu \"%s\"\n", node->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    %s %s = ", type_name, name) ||
        !emit_expression(program, node->left_child, source, errors) ||
        !buffer_append(source, ";\n") ||
        !buffer_appendf(source, "    (void)%s;\n", name)) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}


bool append_print_program(const TypedProgram *program,
                                 const TypedNode *var, size_t store_count,
                                 const char *effective_source,
                                 TextBuffer *source, ErrorState *errors)
{
    char name_text[USER_NAME_CAPACITY];
    const char *name = user_c_name(program, var->name_index,
                                  name_text, sizeof(name_text));

    if (!valid_identifier(name)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }

    if (!buffer_append(source, "int32_t nyx_program_value(void)\n{\n") ||
        !buffer_appendf(source, "#line %zu \"%s\"\n", var->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    int32_t %s = ", name) ||
        !emit_expression(program, var->left_child, source, errors) ||
        !buffer_append(source, ";\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    {
        size_t i;

        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node != NULL && node->kind == TYPED_ASSIGNMENT &&
                !emit_statement(program, root, 1, effective_source, source,
                                errors)) {
                return false;
            }
        }
    }
    if (!buffer_appendf(source, "    return %s;\n", name) ||
        !buffer_append(source, "}\n\nint main(void)\n{\n") ||
        !emit_store_inits(program, store_count, source, errors) ||
        !buffer_append(source,
                       "    printf(\"%d\\n\", (int)nyx_program_value());\n") ||
        !emit_store_frees(store_count, source, errors) ||
        !buffer_append(source, "    return 0;\n}\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    return true;
}


bool emit_store_inits(const TypedProgram *program, size_t store_count,
                             TextBuffer *source, ErrorState *errors)
{
    size_t id;

    for (id = 0; id < store_count; id++) {
        size_t declaration_index = find_store_declaration(program, id);
        const TypedNode *declaration =
            typed_program_node(program, declaration_index);

        if (declaration == NULL || declaration->kind != TYPED_STORE_DECLARATION ||
            declaration->integer_value < 0) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            return false;
        }
        if (!buffer_appendf(source,
                            "    nyx_store_init(&nyx_stores[%zu], %d);\n",
                            id, declaration->integer_value)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


bool emit_store_frees(size_t store_count, TextBuffer *source,
                             ErrorState *errors)
{
    size_t id;

    for (id = 0; id < store_count; id++) {
        if (!buffer_appendf(source, "    nyx_store_free(&nyx_stores[%zu]);\n",
                            id)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}
