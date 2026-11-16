#include "generator_internal.h"

static void emit_numeric_equality_nippy_type_is_signed_integer(const TypedType *a, const TypedType *b, bool *signed_unsigned, bool *signed_float, bool *unsigned_float)
{
    if ((nippy_type_is_signed_integer(a->kind) &&
         nippy_type_is_unsigned_integer(b->kind)) ||
        (nippy_type_is_unsigned_integer(a->kind) &&
         nippy_type_is_signed_integer(b->kind))) {
        (*signed_unsigned) = true;
    }
    if ((nippy_type_is_signed_integer(a->kind) && nippy_type_is_float(b->kind)) ||
         (nippy_type_is_float(a->kind) && nippy_type_is_signed_integer(b->kind))) {
        (*signed_float) = true;
    }
    if ((nippy_type_is_unsigned_integer(a->kind) && nippy_type_is_float(b->kind)) ||
         (nippy_type_is_float(a->kind) && nippy_type_is_unsigned_integer(b->kind))) {
        (*unsigned_float) = true;
    }
}

static bool emit_numeric_equality_unsigned_float(const TypedProgram *program, bool *signed_unsigned, bool *signed_float, TextBuffer *source)
{
    bool unsigned_float = false;
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        const TypedNode *left;
        const TypedNode *right;
        const TypedType *a;
        const TypedType *b;

        if (node == NULL || node->kind != TYPED_BINARY ||
            (node->op != NIPPY_OPERATOR_EQ && node->op != NIPPY_OPERATOR_NE)) {
            continue;
        }
        left = typed_program_node(program, node->left_child);
        right = typed_program_node(program, node->right_child);
        a = left == NULL ? NULL : typed_program_type(program, left->type_index);
        b = right == NULL ? NULL : typed_program_type(program, right->type_index);
        if (a == NULL || b == NULL) {
            continue;
        }
        emit_numeric_equality_nippy_type_is_signed_integer(a, b, signed_unsigned, signed_float, &unsigned_float);
    }
    if ((*signed_unsigned) && !buffer_append(source,
        "static inline bool nyx_eq_su(int64_t s, uint64_t u)\n"
        "{ return s >= 0 && (uint64_t)s == u; }\n"
        "static inline bool nyx_eq_us(uint64_t u, int64_t s)\n"
        "{ return nyx_eq_su(s, u); }\n\n")) {
        return true;
    }
    if ((unsigned_float || (*signed_float)) && !buffer_append(source,
        "static inline bool nyx_eq_uf(uint64_t u, double f)\n"
        "{\n"
        "    if (!(f >= 0.0 && f < 18446744073709551616.0)) return false;\n"
        "    return (uint64_t)f == u && (double)u == f;\n"
        "}\n"
        "static inline bool nyx_eq_fu(double f, uint64_t u)\n"
        "{ return nyx_eq_uf(u, f); }\n\n")) {
        return true;
    }
    return false;
}

/* Emit only the mixed-domain helpers needed by numeric equality. */
static bool emit_numeric_equality(const TypedProgram *program,
                                  TextBuffer *source, ErrorState *errors)
{
    bool signed_unsigned = false;
    bool signed_float = false;
    if (emit_numeric_equality_unsigned_float(program, &signed_unsigned, &signed_float, source)) {
        goto fail;
    }
    if (signed_float && !buffer_append(source,
        "static inline bool nyx_eq_sf(int64_t s, double f)\n"
        "{\n"
        "    if (s < 0) return f < 0.0 && nyx_eq_uf((uint64_t)(-(s + 1)) + 1, -f);\n"
        "    return nyx_eq_uf((uint64_t)s, f);\n"
        "}\n"
        "static inline bool nyx_eq_fs(double f, int64_t s)\n"
        "{ return nyx_eq_sf(s, f); }\n\n")) {
        goto fail;
    }
    return true;

fail:
    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
    return false;
}


static bool generator_generate_root(const TypedProgram *program, size_t i, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    size_t root = *(const size_t *)list_get(&program->roots, i);
    const TypedNode *node = typed_program_node(program, root);

    if (node->kind == TYPED_DECLARATION &&
        !emit_var_to_main(program, node, effective_source, source,
                          errors)) {
        return true;
    }
    if ((node->kind == TYPED_EXPRESSION_STATEMENT ||
         node->kind == TYPED_SCOPE ||
         node->kind == TYPED_ASSIGNMENT ||
         node->kind == TYPED_LIST_SET ||
         node->kind == TYPED_MEMBER_WRITE ||
         node->kind == TYPED_RECORD_SET ||
         node->kind == TYPED_TABLE_SET ||
         node->kind == TYPED_FOR ||
         node->kind == TYPED_WHILE ||
         node->kind == TYPED_MATCH_BIND_STATEMENT ||
         node->kind == TYPED_IF) &&
        !emit_statement(program, root, 1, effective_source, source,
                        errors)) {
        return true;
    }
    return false;
}

static bool generator_generate_append_print_program(bool print_program, const TypedProgram *program, const TypedNode *single_var, size_t store_count, const char *effective_source, TextBuffer *source, ErrorState *errors, size_t *i)
{
    if (print_program) {
        if (!append_print_program(program, single_var, store_count,
                                  effective_source, source, errors)) {
            return true;
        }
    } else {
        if (!buffer_append(source, "int main(void)\n{\n") ||
            !emit_match_temporaries(program, 0, source, errors) ||
            !emit_scope_frame_declaration(program, TYPED_NO_NODE, source, errors) ||
            !emit_store_inits(program, store_count, effective_source, source, errors)) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return true;
        }
        for ((*i) = 0; (*i) < program->roots.length; (*i)++) {
            if (!emit_scope_frames(program,
                                   *(const size_t *)list_get(&program->roots, (*i)),
                                   source, errors)) {
                return true;
            }
        }
        for ((*i) = 0; (*i) < program->roots.length; (*i)++) {
            if (generator_generate_root(program, (*i), effective_source, source, errors)) {
        return true;
    }
        }
        if (!emit_store_frees(store_count, source, errors) ||
            !buffer_append(source, "    return 0;\n}\n")) {
            if (!errors->reported) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            }
            return true;
        }
    }
    return false;
}

static void generator_generate_program_uses_binary_operator(bool *uses_arith_helpers, const TypedProgram *program, bool *uses_map, bool *uses_table, bool *uses_range, bool *uses_store_or_handle, bool uses_dynamic_string, bool uses_dynamic_list, bool uses_slice)
{
    (*uses_arith_helpers) =
        program_uses_binary_operator(program, NIPPY_OPERATOR_DIV) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_MOD) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_SHL) ||
        program_uses_binary_operator(program, NIPPY_OPERATOR_SHR);
    (*uses_map) = false;
    (*uses_table) = false;
    {
        size_t type_i;

        for (type_i = 0; type_i < program->types.length; type_i++) {
            const TypedType *type = typed_program_type(program, type_i);
            if (type != NULL && type->kind == NIPPY_TYPE_RANGE) (*uses_range) = true;

            if (type != NULL && type->kind == NIPPY_TYPE_RECORD) {
                (*uses_map) = true;
            }
            if (type != NULL && type->kind == NIPPY_TYPE_TABLE) {
                (*uses_map) = true;
                (*uses_table) = true;
            }
        }
    }

    (*uses_store_or_handle) = (*uses_store_or_handle) || uses_dynamic_string ||
                           uses_dynamic_list || uses_slice || (*uses_table);
}

static bool generator_generate_arithmetic_runtime(bool uses_arith_helpers, bool uses_conversion, TextBuffer *source, ErrorState *errors, bool uses_store_or_handle, size_t store_count, bool uses_dynamic_string, bool uses_dynamic_list, bool uses_table, const TypedProgram *program)
{
    if ((uses_arith_helpers || uses_conversion) &&
        !emit_arithmetic_runtime(source, errors)) {
        return true;
    }
    if (uses_conversion && !emit_conversion_runtime(source, errors)) {
        return true;
    }
    if (uses_store_or_handle &&
        !emit_store_runtime(source, store_count, uses_dynamic_string,
                            uses_dynamic_list || uses_table, errors)) {
        return true;
    }
    if (program_uses_scope(program) && !buffer_append(source,
        "typedef struct nyx_scope_frame {\n"
        "    nyx_store store;\n"
        "    struct nyx_scope_frame *previous; /* entered scopes, innermost first */\n"
        "    struct nyx_scope_frame *cached;   /* buffers a loop is keeping */\n"
        "    int recycled;\n"
        "} nyx_scope_frame;\n"
        "/* Every entered scope gets a generation no other Store has had, so a\n"
        "   handle kept past its scope cannot match a later one. */\n"
        "static uint64_t nyx_scope_generation = 1;\n"
        "static NIPPY_UNUSED void nyx_scope_enter(nyx_scope_frame **top, nyx_scope_frame *frame, int32_t capacity, const char *file, size_t line, size_t column, size_t pos) {\n"
        "    if (capacity < 0) nyx_panic_at(\"" MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
        "    nyx_store_init_at(&frame->store, (size_t)capacity, file, line, column, pos);\n"
        "    frame->store.generation = ++nyx_scope_generation;\n"
        "    frame->recycled = 0;\n"
        "    frame->previous = *top; *top = frame;\n"
        "}\n"
        "/* Inside a loop: the first entry allocates and the loop keeps the buffer\n"
        "   until it ends; later entries start it empty again. */\n"
        "static NIPPY_UNUSED void nyx_scope_enter_cached(nyx_scope_frame **top, nyx_scope_frame **cache, nyx_scope_frame *frame, int32_t capacity, const char *file, size_t line, size_t column, size_t pos) {\n"
        "    if (capacity < 0) nyx_panic_at(\"" MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
        "    if (frame->store.data == NULL) {\n"
        "        nyx_store_init_at(&frame->store, (size_t)capacity, file, line, column, pos);\n"
        "        frame->cached = *cache; *cache = frame;\n"
        "    } else {\n"
        "        frame->store.used = 0;\n"
        "    }\n"
        "    frame->store.generation = ++nyx_scope_generation;\n"
        "    frame->recycled = 1;\n"
        "    frame->previous = *top; *top = frame;\n"
        "}\n"
        "static NIPPY_UNUSED void nyx_scope_cleanup(nyx_scope_frame **top, nyx_scope_frame *mark) {\n"
        "    while (*top != mark) {\n"
        "        nyx_scope_frame *frame = *top; *top = frame->previous;\n"
        "        if (frame->recycled) { frame->store.used = 0; frame->store.generation = 0; }\n"
        "        else nyx_store_free(&frame->store);\n"
        "    }\n"
        "}\n"
        "static NIPPY_UNUSED void nyx_scope_release(nyx_scope_frame **cache, nyx_scope_frame *mark) {\n"
        "    while (*cache != mark) { nyx_scope_frame *frame = *cache; *cache = frame->cached; nyx_store_free(&frame->store); }\n"
        "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (program_uses_string(program) &&
        !emit_string_runtime(program, source, errors)) {
        return true;
    }
    if (uses_dynamic_string &&
        !emit_dynamic_string_runtime(program, source, errors)) {
        return true;
    }
    return false;
}

static bool generator_generate_dynamic_string_literals(bool uses_dynamic_string, const TypedProgram *program, TextBuffer *source, ErrorState *errors, bool uses_string_builder, bool uses_list, bool uses_slice, bool uses_dynamic_list, bool uses_map, size_t store_count, bool uses_print)
{
    if (uses_dynamic_string &&
        !emit_dynamic_string_literals(program, source, errors)) {
        return true;
    }
    if (uses_string_builder &&
        !emit_builder_runtime(source, errors)) {
        return true;
    }
    if (uses_list && !emit_list_runtime(program, source,
                                        errors)) {
        return true;
    }
    if (uses_slice && uses_dynamic_list &&
        !emit_slice_runtime(program, source, errors)) {
        return true;
    }
    if (uses_map && !emit_map_runtime(program, store_count, source, errors)) {
        return true;
    }
    if (!emit_equality_helpers(program, source, errors)) {
        return true;
    }
    if (uses_print && !emit_print_helpers(program, source, errors)) {
        return true;
    }
    return false;
}

static bool generator_generate_buffer_append(bool uses_range, TextBuffer *source, ErrorState *errors, bool uses_recursion, bool uses_store_or_handle, bool uses_dynamic_string, bool uses_list, bool uses_slice, bool uses_arith_helpers, bool uses_conversion, const TypedProgram *program, size_t store_count, bool uses_dynamic_list, bool uses_table)
{
    if (uses_range && !buffer_append(source,
        "static inline int64_t nyx_range_step(int64_t step, const char *file, "
        "size_t line, size_t col, size_t pos) {\n"
        "    if (step == 0) { fflush(stdout); "
        "fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", "
        "file, line, col, pos, \"" MESSAGE_RUNTIME_RANGE_ZERO_STEP "\"); exit(1); }\n"
        "    return step;\n}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (uses_recursion &&
        !buffer_appendf(source, "#define NIPPY_RECURSION_LIMIT %d\n\n",
                        NIPPY_RUNTIME_RECURSION_LIMIT)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (uses_store_or_handle || uses_dynamic_string || uses_list || uses_slice ||
        uses_arith_helpers || uses_conversion) {
        if (!buffer_append(source,
                           "#if defined(__GNUC__) || defined(__clang__)\n"
                           "#define NIPPY_UNUSED __attribute__((unused))\n"
                           "#else\n"
                           "#define NIPPY_UNUSED\n"
                           "#endif\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return true;
        }
    }
    if (!emit_numeric_equality(program, source, errors)) {
        return true;
    }
    if (generator_generate_arithmetic_runtime(uses_arith_helpers, uses_conversion, source, errors, uses_store_or_handle, store_count, uses_dynamic_string, uses_dynamic_list, uses_table, program)) {
        return true;
    }
    return false;
}

static bool generator_generate_buffer_append9(TextBuffer *header, ErrorState *errors, bool uses_map, bool uses_store_or_handle, bool uses_dynamic_string, bool uses_list, bool uses_slice, bool uses_table, const TypedProgram *program, const char *effective_source)
{
    if (!buffer_append(header, "\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (uses_map && !buffer_append(header, "#include <stddef.h>\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }

    if (uses_store_or_handle || uses_dynamic_string || uses_list || uses_slice || uses_table) {
        if (!buffer_append(header, "typedef struct nyx_store nyx_store;\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return true;
        }
    }
    if (uses_store_or_handle) {
        if (!buffer_append(
                header,
                "typedef struct {\n"
                "    nyx_store *store;\n"
                "    size_t offset;\n"
                "    uint64_t generation;\n"
                "} nyx_handle;\n\n"
                "struct nyx_store {\n"
                "    unsigned char *data;\n"
                "    size_t used;\n"
                "    size_t capacity;\n"
                "    uint64_t generation;\n"
                "};\n\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return true;
        }
    }

    if (!emit_type_definitions(program, effective_source, header, errors)) {
        return true;
    }
    return false;
}

static bool generator_generate_function_typedefs(const TypedProgram *program, TextBuffer *header, ErrorState *errors, bool has_expression_statement, size_t var_count, const TypedNode *single_var, bool *print_program, TextBuffer *source, const char *effective_header)
{
    if (!emit_function_typedefs(program, header, errors)) {
        return true;
    }

    if (!has_expression_statement && var_count == 1 &&
        typed_program_type(program, single_var->type_index) != NULL &&
        (typed_program_type(program, single_var->type_index)->kind ==
             NIPPY_TYPE_INT32 ||
         typed_program_type(program, single_var->type_index)->kind ==
             NIPPY_TYPE_INT64)) {
        (*print_program) = true;
    }

    if ((*print_program) &&
        !buffer_appendf(header, "%s nyx_program_value(void);\n\n",
                        typed_program_type(program, single_var->type_index)->kind ==
                                NIPPY_TYPE_INT64 ? "int64_t" : "int32_t")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (!buffer_append(header, "#endif\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }

    if (!buffer_appendf(source, "#include \"%s\"\n\n", effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    return false;
}

static bool generator_generate_buffer_append12(TextBuffer *header, ErrorState *errors, bool uses_store_or_handle, bool uses_dynamic_string, bool uses_list, bool uses_slice, const TypedProgram *program, bool uses_map, bool uses_table, const char *effective_source)
{
    if (!buffer_append(header,
                       "#ifndef NIPPY_GENERATED_H\n"
                       "#define NIPPY_GENERATED_H\n\n"
                       "#include <stdbool.h>\n"
                       "#include <stdint.h>\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (uses_store_or_handle || uses_dynamic_string || uses_list ||
        uses_slice || program_uses_string(program)) {
        if (!buffer_append(header, "#include <stddef.h>\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return true;
        }
    }
    if (generator_generate_buffer_append9(header, errors, uses_map, uses_store_or_handle, uses_dynamic_string, uses_list, uses_slice, uses_table, program, effective_source)) {
        return true;
    }
    return false;
}

static int generator_generate_valid_header_name(const char *effective_header, ErrorState *errors, const char *effective_source, TextBuffer *header, TextBuffer *source, size_t *store_count, const TypedProgram *program, bool *uses_store_or_handle, bool *uses_string_builder, bool *uses_dynamic_string)
{
    if (!valid_header_name(effective_header)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME);
        return 2;
    }
    if (!valid_line_name(effective_source)) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_LINE_NAME);
        return 2;
    }

    buffer_init(header);
    buffer_init(source);

    (*store_count) = count_store_declarations(program);
    if (!validate_store_declarations(program, (*store_count), errors)) {
        return 1;
    }
    (*uses_store_or_handle) =
        (*store_count) > 0 || program_uses_handle_or_store_type(program);
    (*uses_string_builder) = program_uses_string_builder(program);
    (*uses_dynamic_string) =
        program_uses_dynamic_string(program) || (*uses_string_builder);
    return 0;
}

static bool generator_generate_root15(size_t *i, const TypedProgram *program, const char *effective_source, TextBuffer *source, ErrorState *errors, bool print_program, const TypedNode *single_var, size_t store_count)
{
    for ((*i) = 0; (*i) < program->roots.length; (*i)++) {
        size_t root = *(const size_t *)list_get(&program->roots, (*i));
        const TypedNode *node = typed_program_node(program, root);

        if (node->kind == TYPED_FUNCTION_DECLARATION &&
            !emit_function(program, node, effective_source, source, errors)) {
            return true;
        }
    }

    if (generator_generate_append_print_program(print_program, program, single_var, store_count, effective_source, source, errors, i)) {
        return true;
    }
    return false;
}

static bool generator_generate_buffer_append17(TextBuffer *source, ErrorState *errors, bool uses_conversion)
{
    if (!buffer_append(source,
                       "#include <stdio.h>\n"
                       "#include <stdlib.h>\n"
                       "#include <string.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    if (uses_conversion &&
        !buffer_append(source, "#include <float.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    return false;
}

static bool generator_generate_program_uses_string(bool uses_store_or_handle, const TypedProgram *program, bool uses_dynamic_string, bool uses_list, bool uses_slice, bool uses_print, bool uses_map, bool uses_arith_helpers, bool uses_recursion, bool uses_conversion, bool uses_range, TextBuffer *source, ErrorState *errors, bool print_program)
{
    if (uses_store_or_handle || program_uses_string(program) ||
        uses_dynamic_string || uses_list || uses_slice || uses_print ||
        uses_map || uses_arith_helpers || uses_recursion ||
        uses_conversion || uses_range) {
        if (generator_generate_buffer_append17(source, errors, uses_conversion)) {
        return true;
    }
    } else if (print_program &&
               !buffer_append(source, "#include <stdio.h>\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return true;
    }
    return false;
}

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
    bool uses_range = false;
    bool uses_map;
    bool uses_table;
    bool uses_arith_helpers;
    size_t store_count;
    size_t i;
    bool ok = false;

    *c_source = NULL;
    *h_source = NULL;

    {
        int status = generator_generate_valid_header_name(effective_header, errors, effective_source, &header, &source, &store_count, program, &uses_store_or_handle, &uses_string_builder, &uses_dynamic_string);

        if (status == 1) {
            goto done;
        }
        if (status == 2) {
            return false;
        }
    }
    uses_list = program_uses_list(program);
    uses_dynamic_list = program_uses_dynamic_list(program);
    uses_slice = program_uses_slice(program);
    uses_print = program_uses_print(program);
    uses_recursion = program_uses_recursion(program);
    uses_conversion = program_uses_numeric_conversion(program);
    generator_generate_program_uses_binary_operator(&uses_arith_helpers, program, &uses_map, &uses_table, &uses_range, &uses_store_or_handle, uses_dynamic_string, uses_dynamic_list, uses_slice);

    if (generator_generate_buffer_append12(&header, errors, uses_store_or_handle, uses_dynamic_string, uses_list, uses_slice, program, uses_map, uses_table, effective_source)) {
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
        } else if (node->kind == TYPED_SCOPE ||
                   node->kind == TYPED_ASSIGNMENT ||
                   node->kind == TYPED_LIST_SET ||
                   node->kind == TYPED_MEMBER_WRITE ||
                   node->kind == TYPED_RECORD_SET ||
                   node->kind == TYPED_TABLE_SET ||
                   node->kind == TYPED_FOR ||
                   node->kind == TYPED_WHILE ||
                   node->kind == TYPED_MATCH_BIND_STATEMENT ||
                   node->kind == TYPED_IF) {
            /* An assignment changes the printed value instead of replacing the
               program-value entry point. */
        } else if (node->kind != TYPED_FUNCTION_DECLARATION) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT);
            goto done;
        }
    }

    if (generator_generate_function_typedefs(program, &header, errors, has_expression_statement, var_count, single_var, &print_program, &source, effective_header)) {
        goto done;
    }
    if (generator_generate_program_uses_string(uses_store_or_handle, program, uses_dynamic_string, uses_list, uses_slice, uses_print, uses_map, uses_arith_helpers, uses_recursion, uses_conversion, uses_range, &source, errors, print_program)) {
        goto done;
    }
    if (generator_generate_buffer_append(uses_range, &source, errors, uses_recursion, uses_store_or_handle, uses_dynamic_string, uses_list, uses_slice, uses_arith_helpers, uses_conversion, program, store_count, uses_dynamic_list, uses_table)) {
        goto done;
    }
    if (generator_generate_dynamic_string_literals(uses_dynamic_string, program, &source, errors, uses_string_builder, uses_list, uses_slice, uses_dynamic_list, uses_map, store_count, uses_print)) {
        goto done;
    }

    if (generator_generate_root15(&i, program, effective_source, &source, errors, print_program, single_var, store_count)) {
        goto done;
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


static bool append_print_program_i(const TypedProgram *program, const char *effective_source, TextBuffer *source, ErrorState *errors)
{
    {
        size_t i;

        for (i = 0; i < program->roots.length; i++) {
            size_t root = *(const size_t *)list_get(&program->roots, i);
            const TypedNode *node = typed_program_node(program, root);

            if (node != NULL &&
                (node->kind == TYPED_SCOPE ||
                 node->kind == TYPED_ASSIGNMENT ||
                 node->kind == TYPED_LIST_SET ||
                 node->kind == TYPED_MEMBER_WRITE ||
                 node->kind == TYPED_RECORD_SET ||
                 node->kind == TYPED_TABLE_SET ||
                 node->kind == TYPED_FOR || node->kind == TYPED_WHILE ||
                 node->kind == TYPED_IF ||
                 node->kind == TYPED_MATCH_BIND_STATEMENT) &&
                !emit_statement(program, root, 1, effective_source, source,
                                errors)) {
                return false;
            }
        }
    }
    return true;
}

static bool append_print_program_name(const TypedProgram *program, const TypedNode *var, char (*name_text)[48], ErrorState *errors, TextBuffer *source, const char *effective_source, const char **name, bool *wide)
{
    (*name) = user_c_name(program, var->name_index,
                                  (*name_text), sizeof((*name_text)));
    const TypedType *type = typed_program_type(program, var->type_index);
    (*wide) = type != NULL && type->kind == NIPPY_TYPE_INT64;

    if (!valid_identifier((*name))) {
        error_report(errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER);
        return false;
    }

    if (!buffer_appendf(source, "%s nyx_program_value(void)\n{\n",
                        (*wide) ? "int64_t" : "int32_t") ||
        !emit_match_temporaries(program, 0, source, errors) ||
        !emit_scope_frame_declaration(program, TYPED_NO_NODE, source, errors) ||
        !buffer_appendf(source, "#line %zu \"%s\"\n", var->pos.line,
                        effective_source) ||
        !buffer_appendf(source, "    %s %s = ",
                        (*wide) ? "int64_t" : "int32_t", (*name)) ||
        !emit_expression(program, var->left_child, source, errors) ||
        !buffer_append(source, ";\n")) {
        if (!errors->reported) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        }
        return false;
    }
    if (!append_print_program_i(program, effective_source, source, errors)) {
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
    const char *name = {0};
    bool wide = {0};
    if (!append_print_program_name(program, var, &name_text, errors, source, effective_source, &name, &wide)) {
        return false;
    }
    if (!buffer_appendf(source, "    return %s;\n", name) ||
        !buffer_append(source, "}\n\nint main(void)\n{\n") ||
        !emit_store_inits(program, store_count, effective_source, source, errors) ||
        !buffer_append(source, wide
            ? "    printf(\"%lld\\n\", (long long)nyx_program_value());\n"
            : "    printf(\"%d\\n\", (int)nyx_program_value());\n") ||
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
                      const char *effective_source, TextBuffer *source,
                      ErrorState *errors)
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
        if (!buffer_appendf(source, "#line %zu \"%s\"\n",
                            declaration->pos.line, effective_source) ||
            !buffer_appendf(source,
                            "    nyx_store_init_at(&nyx_stores[%zu], %d, __FILE__, %zu, %zu, %zu);\n",
                            id, declaration->integer_value, declaration->pos.line,
                            declaration->pos.column, declaration->pos.offset)) {
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
