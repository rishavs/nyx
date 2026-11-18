#include "generator_internal.h"


static bool emit_print_helper_declaration_child(const TypedProgram *program, const TypedNode *declaration, size_t *i, ErrorState *errors, TextBuffer *source)
{
    size_t child = *(const size_t *)list_get(
        &program->children, declaration->children_start + (*i));
    const TypedNode *field = typed_program_node(program, child);
    const char *field_name =
        field == NULL
            ? NULL
            : typed_program_symbol_text(program, field->name_index);

    char member_text[USER_NAME_CAPACITY];
    const char *member = field == NULL
                             ? NULL
                             : user_c_name(program, field->name_index,
                                           member_text,
                                           sizeof(member_text));

    if (field == NULL || field_name == NULL || member == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    /* The text shows the name the program used; the C member access
       needs the name the generator emitted. */
    buffer_appendf(source, "    fputs(\"%s%s: \", stdout);\n",
                    (*i) == 0 ? "" : ", ", field_name);
    buffer_appendf(source,
                    "    nyx_print_%zu(value.%s, depth + 1, file, line, column, pos);\n",
                    field->type_index, member);
    return true;
}

static bool emit_print_helper_declaration(const TypedProgram *program, const TypedType *type, ErrorState *errors, TextBuffer *source, size_t *i)
{
    const TypedNode *declaration = typed_program_node(
        program, type->declaration_node);
    const char *name = type->source_name_index == TYPED_NO_NODE
                           ? MESSAGE_FRAGMENT_STRUCT
                           : typed_program_symbol_text(
                                 program, type->source_name_index);

    if (declaration == NULL || name == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    buffer_appendf(source, "    fputs(\"%s { \", stdout);\n", name);
    for ((*i) = 0; (*i) < declaration->children_count; (*i)++) {
        if (!emit_print_helper_declaration_child(program, declaration, i, errors, source)) {
        return false;
    }
    }
    buffer_append(source, "    fputs(\" }\", stdout);\n");
    return true;
}

static void emit_print_helper_names(TextBuffer *source, size_t *i, const TypedType *type, const TypedProgram *program)
{
    static const char *const names[] = {
        "header", "description", "filename", "line", "col", "pos"
    };

    buffer_append(source, "    fputs(\"Error(\", stdout);\n");
    for ((*i) = 0; (*i) < type->child_count && (*i) < 6; (*i)++) {
        size_t field = *(const size_t *)list_get(
            &program->type_children, type->first_child + (*i));

        buffer_appendf(source, "    fputs(\"%s\", stdout);\n",
                        (*i) == 0 ? "" : ", ");
        buffer_appendf(source,
                        "    nyx_print_%zu(value.%s, depth + 1, file, line, column, pos);\n",
                        field, names[(*i)]);
    }
    buffer_appendf(source,
                    "    fprintf(stdout, \", %%d, %%d, %%d\", "
                    "(int)value.line, (int)value.col, (int)value.pos);\n"
                    "    fputs(\")\", stdout);\n");
}

static bool emit_print_helper_alternative(const TypedProgram *program, const TypedType *type, size_t i, ErrorState *errors, TextBuffer *source)
{
    size_t alternative = *(const size_t *)list_get(
        &program->type_children, type->first_child + i);
    const TypedType *alternative_type = typed_program_type(
        program, alternative);

    if (alternative_type == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    buffer_appendf(source, "    case %zu:\n", i);
    if (alternative_type->kind == NYX_TYPE_NIL) {
        buffer_append(source,
                       "        fputs(\"nil\", stdout);\n");
    } else if (!buffer_appendf(
                   source,
                   "        nyx_print_%zu(value.value.v%zu, "
                   "depth + 1, file, line, column, pos);\n",
                   alternative, i)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    buffer_append(source, "        break;\n");
    return true;
}

static void emit_print_helper_key(const TypedProgram *program, const TypedType *type, TextBuffer *source)
{
    size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, type->first_child + 1);
    size_t buckets = type->fixed_length == 0 ? 1 : type->fixed_length + 1;

    buffer_append(source,
                   "    fputs(\"{\", stdout);\n"
                   "    size_t count = 0;\n"
                   "    for (size_t i = 0; i < ");
    buffer_appendf(source, "%zu", buckets);
    buffer_append(source,
                   "; i++) {\n"
                   "        if (value.states[i] != 1) continue;\n"
                   "        if (count++ != 0) fputs(\", \", stdout);\n");
    buffer_appendf(source, "        nyx_print_%zu(value.keys[i], "
                            "depth + 1, file, line, column, pos);\n"
                            "        fputs(\": \", stdout);\n"
                            "        nyx_print_%zu(value.values[i], "
                            "depth + 1, file, line, column, pos);\n"
                            "    }\n"
                            "    fputs(\"}\", stdout);\n",
                    key_type, value_type);
}

static bool emit_print_helper_element(const TypedProgram *program, const TypedType *type, ErrorState *errors, TextBuffer *source)
{
    size_t element = *(const size_t *)list_get(&program->type_children,
                                               type->first_child);
    char element_name[NYX_IDENTIFIER_MAX + 32];
    const char *get = type->kind == NYX_TYPE_LIST
                          ? "nyx_list_len_at(value, file, line, column, pos)"
                          : "nyx_slice_len_at(value, file, line, column, pos)";
    const char *data = type->kind == NYX_TYPE_LIST
                           ? "nyx_list_data_at"
                           : "nyx_slice_data_at";

    if (!format_c_type(program, element, element_name,
                       sizeof(element_name), errors)) {
        return false;
    }
    buffer_appendf(source,
                    "    fputs(\"[\", stdout);\n"
                    "    for (size_t i = 0; i < (size_t)%s; i++) {\n"
                    "        if (i != 0) fputs(\", \", stdout);\n"
                    "        nyx_print_%zu(*(%s *)%s(value, i, "
                    "sizeof(%s), file, line, column, pos), depth + 1, file, line, column, pos);\n"
                    "    }\n"
                    "    fputs(\"]\", stdout);\n",
                    get, element, element_name, data, element_name);
    return true;
}

static void emit_print_helper_buffer_append(TextBuffer *source, size_t element)
{
    buffer_append(source,
                   "    fputs(\"[\", stdout);\n"
                   "    for (size_t i = 0; i < value.length; i++) {\n"
                   "        if (i != 0) fputs(\", \", stdout);\n");
    buffer_appendf(source,
                    "        nyx_print_%zu(value.data[i], "
                    "depth + 1, file, line, column, pos);\n",
                    element);
    buffer_append(source,
                   "    }\n"
                   "    fputs(\"]\", stdout);\n");
}

static bool emit_print_helper_key9(const TypedProgram *program, const TypedType *type, ErrorState *errors, TextBuffer *source, size_t type_index)
{
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
    buffer_appendf(source,
    "    nyx_table_state_%zu *state = nyx_table_%zu_state_at(value, file, line, column, pos);\n"
    "    nyx_store *store = value.store;\n"
    "    %s *keys = (%s *)(void *)(store->data + state->keys_offset);\n"
    "    %s *values = (%s *)(void *)(store->data + state->values_offset);\n"
    "    uint8_t *states = (uint8_t *)(void *)(store->data + "
    "state->states_offset);\n"
    "    fputs(\"{\", stdout);\n"
    "    size_t count = 0;\n"
    "    for (size_t i = 0; i < state->buckets; i++) {\n"
    "        if (states[i] != 1) continue;\n"
    "        if (count++ != 0) fputs(\", \", stdout);\n"
    "        nyx_print_%zu(keys[i], depth + 1, file, line, column, pos);\n"
    "        fputs(\": \", stdout);\n"
    "        nyx_print_%zu(values[i], depth + 1, file, line, column, pos);\n"
    "    }\n"
    "    fputs(\"}\", stdout);\n",
    type_index, type_index, key_name, key_name, value_name,
    value_name, key_type, value_type);
    return true;
}

static bool emit_print_helper_i(GeneratorContext *gen, const TypedType *type, const TypedProgram *program, size_t type_index, char (*c_type)[64], ErrorState *errors, TextBuffer *source)
{
    (void)gen;
    if (type == NULL ||
        !format_c_type(program, type_index, (*c_type), sizeof((*c_type)), errors)) {
        return false;
    }
    buffer_appendf(source,
                    "static inline void nyx_print_%zu(%s value, int depth, const char *file, size_t line, size_t column, size_t pos)\n"
                    "{\n"
                    "    (void)value; (void)file; (void)line; (void)column; (void)pos;\n"
                    "    if (depth > %d) {\n"
                    "        fputs(\"" MESSAGE_RUNTIME_PRINT_TRUNCATED "\", stdout);\n"
                    "        return;\n"
                    "    }\n",
                    type_index, (*c_type), program->max_print_depth);
    return true;
}

/* The body of a print helper, by kind of type. A scalar's body is a fixed
   piece of C; a kind with parts has a writer that spells them out. */
typedef bool (*PrintBody)(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                          size_t type_index, TextBuffer *source,
                          ErrorState *errors);

typedef struct {
    const char *text;
    PrintBody write;
} PrintRule;

#define PRINT_INT "    fprintf(stdout, \"%d\", (int)value);\n"
#define PRINT_UNSIGNED "    fprintf(stdout, \"%u\", (unsigned)value);\n"

static bool print_float(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                        size_t type_index, TextBuffer *source,
                        ErrorState *errors)
{
    (void)errors;
    (void)gen;
    (void)program;
    (void)type_index;
    buffer_appendf(source, "    nyx_print_float((double)value, %d);\n",
                    type->kind == NYX_TYPE_FLOAT32);
    return true;
}

static bool print_handle(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                         size_t type_index, TextBuffer *source,
                         ErrorState *errors)
{
    (void)errors;
    (void)program;
    (void)type;
    (void)type_index;
    buffer_append(source, "    size_t store_id = 0;\n");
    if ((gen->analysis->store_count > 0 &&
     !buffer_append(source,
                    "    while (store_id < NYX_STORE_COUNT && "
                    "value.store != &nyx_stores[store_id]) store_id++;\n"))) {
        return false;
    }
    buffer_append(source,
                   "    fprintf(stdout, \"" MESSAGE_RUNTIME_PRINT_HANDLE "\", "
                   "store_id, value.offset, "
                   "(unsigned long long)value.generation);\n");
    return true;
}

static bool print_struct(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                         size_t type_index, TextBuffer *source,
                         ErrorState *errors)
{
    (void)gen;
    size_t i;

    (void)type_index;
    return emit_print_helper_declaration(program, type, errors, source, &i);
}

static bool print_union(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                        size_t type_index, TextBuffer *source,
                        ErrorState *errors)
{
    (void)gen;
    size_t i;

    (void)type_index;
    buffer_append(source, "    switch (value.tag) {\n");
    for (i = 0; i < type->child_count; i++) {
        if (!emit_print_helper_alternative(program, type, i, errors, source)) {
            return false;
        }
    }
    buffer_append(source,
                   "    default:\n"
                   "        fputs(\"" MESSAGE_RUNTIME_PRINT_INVALID_UNION "\", stdout);\n"
                   "        break;\n"
                   "    }\n");
    return true;
}

static bool print_error(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                        size_t type_index, TextBuffer *source,
                        ErrorState *errors)
{
    (void)errors;
    (void)gen;
    size_t i;

    (void)type_index;
    emit_print_helper_names(source, &i, type, program);
    return true;
}

static bool print_array(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                        size_t type_index, TextBuffer *source,
                        ErrorState *errors)
{
    (void)errors;
    (void)gen;
    (void)type_index;
    emit_print_helper_buffer_append(
        source,
        *(const size_t *)list_get(&program->type_children, type->first_child));
    return true;
}

static bool print_sequence(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                           size_t type_index, TextBuffer *source,
                           ErrorState *errors)
{
    (void)gen;
    (void)type_index;
    return emit_print_helper_element(program, type, errors, source);
}

static bool print_record(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                         size_t type_index, TextBuffer *source,
                         ErrorState *errors)
{
    (void)errors;
    (void)gen;
    (void)type_index;
    emit_print_helper_key(program, type, source);
    return true;
}

static bool print_table(const TypedProgram *program, GeneratorContext *gen, const TypedType *type,
                        size_t type_index, TextBuffer *source,
                        ErrorState *errors)
{
    (void)gen;
    return emit_print_helper_key9(program, type, errors, source, type_index);
}

static const PrintRule print_rules[NYX_TYPE_KIND_COUNT] = {
    [NYX_TYPE_INT8] = {PRINT_INT, NULL},
    [NYX_TYPE_INT16] = {PRINT_INT, NULL},
    [NYX_TYPE_INT32] = {PRINT_INT, NULL},
    [NYX_TYPE_INT64] = {"    fprintf(stdout, \"%lld\", (long long)value);\n",
                        NULL},
    [NYX_TYPE_BYTE] = {PRINT_UNSIGNED, NULL},
    [NYX_TYPE_UINT16] = {PRINT_UNSIGNED, NULL},
    [NYX_TYPE_UINT32] = {PRINT_UNSIGNED, NULL},
    [NYX_TYPE_UINT64] = {"    fprintf(stdout, \"%llu\", "
                         "(unsigned long long)value);\n", NULL},
    [NYX_TYPE_FLOAT32] = {NULL, print_float},
    [NYX_TYPE_FLOAT64] = {NULL, print_float},
    [NYX_TYPE_BOOL] = {"    fputs(value ? \"true\" : \"false\", stdout);\n",
                       NULL},
    [NYX_TYPE_NIL] = {"    fputs(\"nil\", stdout);\n", NULL},
    [NYX_TYPE_FUNCTION] = {"    fputs(\"" MESSAGE_RUNTIME_PRINT_FUNCTION "\", stdout);\n", NULL},
    [NYX_TYPE_DYNAMIC_STRING] = {
        "    const uint8_t *data = nyx_string_data_at(value, file, line, column, pos);\n"
        "    if (value.length != 0) {\n"
        "        fwrite(data, 1, value.length, stdout);\n"
        "    }\n", NULL},
    [NYX_TYPE_STRING] = {
        "    if (value.length != 0) {\n"
        "        fwrite(value.data, 1, value.length, stdout);\n"
        "    }\n", NULL},
    [NYX_TYPE_TEXT_BUILDER] = {
        "    nyx_builder_state *state = "
        "nyx_builder_get_state(value, file, line, column, pos);\n"
        "    nyx_store *store = nyx_builder_store(value, file, line, column, pos);\n"
        "    if (state->length != 0) {\n"
        "        fwrite(store->data + state->offset, 1, state->length, stdout);\n"
        "    }\n", NULL},
    [NYX_TYPE_HANDLE] = {NULL, print_handle},
    [NYX_TYPE_STRUCT] = {NULL, print_struct},
    [NYX_TYPE_UNION] = {NULL, print_union},
    [NYX_TYPE_ERROR] = {NULL, print_error},
    [NYX_TYPE_FIXED_LIST] = {NULL, print_array},
    [NYX_TYPE_LIST] = {NULL, print_sequence},
    [NYX_TYPE_SLICE] = {NULL, print_sequence},
    [NYX_TYPE_RECORD] = {NULL, print_record},
    [NYX_TYPE_TABLE] = {NULL, print_table},
};

#undef PRINT_INT
#undef PRINT_UNSIGNED

bool emit_print_helper(GeneratorContext *gen, TextBuffer *source, const TypedProgram *program,
                       size_t type_index, ErrorState *errors)
{
    const TypedType *type = typed_program_type(program, type_index);
    char c_type[NYX_IDENTIFIER_MAX + 32];
    const PrintRule *rule;
    bool ok;

    if (!emit_print_helper_i(gen, type, program, type_index, &c_type, errors,
                             source)) {
        return false;
    }
    rule = &print_rules[type->kind];
    if (rule->text == NULL && rule->write == NULL) {
        error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE);
        return false;
    }
    ok = rule->text != NULL
             ? buffer_append(source, rule->text)
             : rule->write(program, gen, type, type_index, source, errors);
    buffer_append(source, "}\n\n");
    return ok;
}


/* The forward declarations and the bodies of the print helpers, for the types
   the analyser found print needs, in the order it found them. */
bool emit_print_helpers(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                               ErrorState *errors)
{
    const List *types = &gen->analysis->print_types;
    size_t i;

    for (i = 0; i < types->length; i++) {
        size_t type_index = *(const size_t *)list_get(types, i);
        char c_type[NYX_IDENTIFIER_MAX + 32];

        if (!format_c_type(program, type_index, c_type, sizeof(c_type),
                       errors)) {
            return false;
        }
        buffer_appendf(source,
                        "static inline void nyx_print_%zu(%s value, int "
                        "depth, const char *file, size_t line, size_t column, size_t pos);\n",
                        type_index, c_type);
    }
    for (i = 0; i < types->length; i++) {
        if (!emit_print_helper(gen, source, program,
                               *(const size_t *)list_get(types, i), errors)) {
            return false;
        }
    }
    return types->length == 0 || buffer_append(source, "\n");
}


static bool emit_print_statement_child(size_t *i, const TypedNode *node, const TypedProgram *program, ErrorState *errors, TextBuffer *source, size_t depth)
{
    for ((*i) = 0; (*i) < node->children_count; (*i)++) {
        size_t child = *(const size_t *)list_get(
            &program->children, node->children_start + (*i));
        const TypedNode *argument = typed_program_node(program, child);
        char c_type[NYX_IDENTIFIER_MAX + 32];

        if (argument == NULL ||
            !format_c_type(program, argument->type_index, c_type,
                           sizeof(c_type), errors)) {
            error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
            return false;
        }
        append_indent(source, depth + 1);
        buffer_appendf(source, "%s nyx_print_arg_%zu_%zu = ",
                        c_type, node->pos.offset, (*i));
        if (!emit_expression(program, child, source, errors)) {
            return false;
        }
        buffer_append(source, ";\n");
        if ((*i) != 0) {
            append_indent(source, depth + 1);
            buffer_append(source, "fputc(' ', stdout);\n");
        }
        append_indent(source, depth + 1);
        buffer_appendf(source,
                        "nyx_print_%zu(nyx_print_arg_%zu_%zu, 0, __FILE__, %zu, %zu, %zu);\n",
                        argument->type_index, node->pos.offset, (*i),
                        node->pos.line, node->pos.column, node->pos.offset);
    }
    return true;
}

bool emit_print_statement(const TypedProgram *program, GeneratorContext *gen,
                                 const TypedNode *node, TextBuffer *source,
                                 ErrorState *errors, size_t depth)
{
    size_t i;

    /* One print is many stdio calls. With threads, hold the stream for the
       whole statement so lines from different threads never interleave. */
    bool lock = analysis_has(gen->analysis, ANALYSIS_PARALLEL);

    append_indent(source, depth);
    buffer_append(source, "{\n");
    if ((lock && (!append_indent(source, depth + 1) ||
              !buffer_append(source, "flockfile(stdout);\n")))) {
        return false;
    }
    if (!emit_print_statement_child(&i, node, program, errors, source, depth)) {
        return false;
    }
    append_indent(source, depth + 1);
    buffer_append(source, "fputc('\\n', stdout);\n");
    if ((lock && (!append_indent(source, depth + 1) ||
              !buffer_append(source, "funlockfile(stdout);\n")))) {
        return false;
    }
    append_indent(source, depth);
    buffer_append(source, "}\n");
    return true;
}
