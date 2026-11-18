#include "generator_internal.h"

/* `for` loops. Each kind of iterable is a shape of C: a range counts a cursor,
   a list, array, slice or string indexes its elements, and a Record or Table
   walks its buckets. Every loop evaluates its iterable once, into a local named
   by the loop's source offset, so nested loops never share a name. */

static bool emit_unsupported_statement(ErrorState *errors)
{
    error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT);
    return false;
}

/* What every loop is made of. */
typedef struct {
    const TypedProgram *program;
    GeneratorContext *gen;
    const TypedNode *node;
    const char *effective_source;
    TextBuffer *source;
    ErrorState *errors;
    size_t depth;
    size_t offset;                  /* names the loop's locals */
    const TypedNode *iterable_node;
    const TypedType *iterable_type;
    const char *value;              /* the loop variable's C name */
    const char *index;              /* the second variable's, or NULL */
    char iterable_c[NYX_IDENTIFIER_MAX + 32];
    char element_c[NYX_IDENTIFIER_MAX + 32];
} Loop;

static void loop_line(const Loop *loop, size_t extra, const char *format, ...)
{
    va_list args;

    append_indent(loop->source, loop->depth + extra);
    va_start(args, format);
    buffer_vappendf(loop->source, format, args);
    va_end(args);
}

/* The scope mark, the body, and the two braces that close the loop. */
static bool loop_finish(const Loop *loop, size_t body_depth)
{
    emit_loop_scope_mark(loop->gen, loop->source, loop->depth + body_depth);
    return emit_loop_body(loop->program, loop->gen, loop->node->right_child,
                          loop->depth + body_depth, loop->effective_source,
                          loop->source, loop->errors);
}

static void loop_close(const Loop *loop)
{
    loop_line(loop, 1, "}\n");
    loop_line(loop, 0, "}\n");
}

/* ---- the second variable of a range, array, list, slice or string ---- */

static void emit_index_variable(const Loop *loop, const char *counter)
{
    if (loop->index != NULL) {
        loop_line(loop, 2, "int32_t %s = (int32_t)%s_%zu;\n", loop->index,
                  counter, loop->offset);
        loop_line(loop, 2, "(void)%s;\n", loop->index);
    }
}

/* ---- ranges ---- */

/* How the cursor advances: a signed one adds the step; an unsigned one adds or
   subtracts its magnitude, so no step overflows. */
static void emit_range_advance(const Loop *loop)
{
    size_t o = loop->offset;

    if (nyx_type_is_signed_integer(
            typed_program_type(loop->program, loop->node->auxiliary_type)
                ->kind)) {
        buffer_appendf(loop->source,
                       "else nyx_cur_%zu = (%s)((int64_t)nyx_cur_%zu + "
                       "nyx_range_%zu.step);\n",
                       o, loop->element_c, o, o);
        return;
    }
    buffer_appendf(loop->source,
                   "else nyx_cur_%zu = (%s)(nyx_range_%zu.step > 0 ? "
                   "(uint64_t)nyx_cur_%zu + nyx_mag_%zu : "
                   "(uint64_t)nyx_cur_%zu - nyx_mag_%zu);\n",
                   o, loop->element_c, o, o, o, o, o);
}

static bool emit_range_for(const Loop *loop)
{
    size_t o = loop->offset;
    bool ok;

    loop_line(loop, 0, "{\n");
    loop_line(loop, 1, "%s nyx_range_%zu = ", loop->iterable_c, o);
    ok = emit_expression(loop->program, loop->node->left_child, loop->source,
                         loop->errors);
    buffer_append(loop->source, ";\n");
    loop_line(loop, 1, "%s nyx_cur_%zu = nyx_range_%zu.start;\n",
              loop->element_c, o, o);
    loop_line(loop, 1, "uint64_t nyx_idx_%zu = 0;\n", o);
    loop_line(loop, 1,
              "bool nyx_active_%zu = nyx_range_%zu.step > 0 ? "
              "nyx_cur_%zu < nyx_range_%zu.end : nyx_cur_%zu > "
              "nyx_range_%zu.end;\n",
              o, o, o, o, o, o);
    loop_line(loop, 1,
              "for (; (void)nyx_idx_%zu, nyx_active_%zu; nyx_idx_%zu++) {\n", o,
              o, o);
    loop_line(loop, 2, "%s %s = nyx_cur_%zu; (void)%s;\n", loop->element_c,
              loop->value, o, loop->value);
    loop_line(loop, 2,
              "uint64_t nyx_dist_%zu = nyx_range_%zu.step > 0 ? "
              "(uint64_t)nyx_range_%zu.end - (uint64_t)nyx_cur_%zu : "
              "(uint64_t)nyx_cur_%zu - (uint64_t)nyx_range_%zu.end;\n",
              o, o, o, o, o, o);
    loop_line(loop, 2,
              "uint64_t nyx_mag_%zu = nyx_range_%zu.step > 0 ? "
              "(uint64_t)nyx_range_%zu.step : UINT64_C(0) - "
              "(uint64_t)nyx_range_%zu.step;\n",
              o, o, o, o);
    loop_line(loop, 2, "if (nyx_mag_%zu >= nyx_dist_%zu) nyx_active_%zu = false;\n",
              o, o, o);
    append_indent(loop->source, loop->depth + 2);
    emit_range_advance(loop);
    if (loop->index != NULL) {
        loop_line(loop, 2, "int32_t %s = (int32_t)nyx_idx_%zu; (void)%s;\n",
                  loop->index, o, loop->index);
    }
    ok = loop_finish(loop, 2) && ok;
    loop_close(loop);
    return ok;
}

/* ---- sequences: lists, arrays, slices and strings ---- */

/* The length of the iterable, read once. */
static bool emit_sequence_length(const Loop *loop)
{
    size_t o = loop->offset;
    const SourcePos *pos = &loop->node->pos;

    switch (loop->iterable_type->kind) {
    case NYX_TYPE_LIST:
        buffer_appendf(loop->source,
                       "nyx_list_len_at(nyx_for_src_%zu, __FILE__, %zu, %zu, "
                       "%zu)",
                       o, pos->line, pos->column, pos->offset);
        return true;
    case NYX_TYPE_FIXED_LIST:
    case NYX_TYPE_STRING:
        buffer_appendf(loop->source, "((int32_t)(nyx_for_src_%zu).length)", o);
        return true;
    case NYX_TYPE_SLICE:
        buffer_appendf(loop->source,
                       "nyx_slice_len_at(nyx_for_src_%zu, __FILE__, %zu, %zu, "
                       "%zu)",
                       o, pos->line, pos->column, pos->offset);
        return true;
    case NYX_TYPE_DYNAMIC_STRING:
        buffer_appendf(loop->source,
                       "nyx_string_len_at(nyx_for_src_%zu, __FILE__, %zu, %zu, "
                       "%zu)",
                       o, pos->line, pos->column, pos->offset);
        return true;
    default:
        return emit_unsupported_statement(loop->errors);
    }
}

/* The element at the current index. */
static bool emit_sequence_element(const Loop *loop)
{
    size_t o = loop->offset;
    const SourcePos *pos = &loop->node->pos;
    const char *element = loop->element_c;

    switch (loop->iterable_type->kind) {
    case NYX_TYPE_FIXED_LIST:
        buffer_appendf(loop->source,
                       "nyx_list_%zu_get(nyx_for_src_%zu, nyx_for_i_%zu, "
                       "__FILE__, %zu, %zu, %zu)",
                       loop->iterable_node->type_index, o, o, pos->line,
                       pos->column, pos->offset);
        return true;
    case NYX_TYPE_LIST:
    case NYX_TYPE_SLICE:
        buffer_appendf(loop->source,
                       "(*(%s *)nyx_%s_data_at(nyx_for_src_%zu, nyx_for_i_%zu, "
                       "sizeof(%s), __FILE__, %zu, %zu, %zu))",
                       element,
                       loop->iterable_type->kind == NYX_TYPE_LIST ? "list"
                                                                  : "slice",
                       o, o, element, pos->line, pos->column, pos->offset);
        return true;
    case NYX_TYPE_STRING:
        buffer_appendf(loop->source,
                       "nyx_string_%zu_at(nyx_for_src_%zu, nyx_for_i_%zu, "
                       "__FILE__, %zu, %zu, %zu)",
                       loop->iterable_type->fixed_length, o, o, pos->line,
                       pos->column, pos->offset);
        return true;
    case NYX_TYPE_DYNAMIC_STRING:
        buffer_appendf(loop->source,
                       "nyx_string_byte_at(nyx_for_src_%zu, nyx_for_i_%zu, "
                       "__FILE__, %zu, %zu, %zu)",
                       o, o, pos->line, pos->column, pos->offset);
        return true;
    default:
        return emit_unsupported_statement(loop->errors);
    }
}

/* A list or array that changes length while it is walked is a failure. */
static void emit_mutation_check(const Loop *loop)
{
    size_t o = loop->offset;
    const SourcePos *pos = &loop->node->pos;

    if (loop->iterable_type->kind == NYX_TYPE_LIST) {
        buffer_appendf(loop->source,
                       "if ((size_t)nyx_list_len_at(nyx_for_src_%zu, __FILE__, "
                       "%zu, %zu, %zu) != nyx_for_len_%zu) { ",
                       o, pos->line, pos->column, pos->offset, o);
    } else {
        buffer_appendf(loop->source,
                       "if ((size_t)(nyx_for_src_%zu).length != "
                       "nyx_for_len_%zu) { ",
                       o, o);
    }
    buffer_append(loop->source,
                  "fflush(stdout); fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION
                  "\\n\", __FILE__, ");
    buffer_appendf(loop->source,
                   "(size_t)%zu, (size_t)%zu, (size_t)%zu, \"%s\" + "
                   "sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1); exit(1); }\n",
                   pos->line, pos->column, pos->offset,
                   MESSAGE_RUNTIME_FOR_MUTATED);
}

static bool emit_sequence_for(const Loop *loop)
{
    size_t o = loop->offset;
    bool check = loop->iterable_type->kind == NYX_TYPE_LIST ||
                 loop->iterable_type->kind == NYX_TYPE_FIXED_LIST;
    bool ok;

    loop_line(loop, 0, "{\n");
    loop_line(loop, 1, "%s nyx_for_src_%zu = ", loop->iterable_c, o);
    ok = emit_expression(loop->program, loop->node->left_child, loop->source,
                         loop->errors);
    buffer_append(loop->source, ";\n");
    loop_line(loop, 1, "size_t nyx_for_len_%zu = (size_t)(", o);
    ok = ok && emit_sequence_length(loop);
    buffer_append(loop->source, ");\n");
    loop_line(loop, 1,
              "for (size_t nyx_for_i_%zu = 0; nyx_for_i_%zu < nyx_for_len_%zu; "
              "nyx_for_i_%zu++) {\n",
              o, o, o, o);
    append_indent(loop->source, loop->depth + 2);
    if (check) {
        emit_mutation_check(loop);
        append_indent(loop->source, loop->depth + 2);
    }
    buffer_appendf(loop->source, "%s %s = ", loop->element_c, loop->value);
    ok = ok && emit_sequence_element(loop);
    buffer_append(loop->source, ";\n");
    loop_line(loop, 2, "(void)%s;\n", loop->value);
    emit_index_variable(loop, "nyx_for_i");
    ok = ok && loop_finish(loop, 2);
    if (check) {
        append_indent(loop->source, loop->depth + 2);
        emit_mutation_check(loop);
    }
    loop_close(loop);
    return ok;
}

/* ---- Records and Tables ---- */

typedef struct {
    Loop *loop;
    char key_c[NYX_IDENTIFIER_MAX + 32];
    char value_c[NYX_IDENTIFIER_MAX + 32];
} MapLoop;

/* One bound variable: `type name = source;` then a use, so an unread variable
   is not a warning. */
static void emit_map_variable(const Loop *loop, const char *type,
                              const char *name, const char *source_format, ...)
{
    va_list args;

    loop_line(loop, 2, "%s %s = ", type, name);
    va_start(args, source_format);
    buffer_vappendf(loop->source, source_format, args);
    va_end(args);
    buffer_append(loop->source, ";\n");
    loop_line(loop, 2, "(void)%s;\n", name);
}

/* A Record keeps its keys, values and states inline. */
static bool emit_record_buckets(MapLoop *map)
{
    Loop *loop = map->loop;
    size_t o = loop->offset;
    size_t buckets = loop->iterable_type->fixed_length == 0
                         ? 1
                         : loop->iterable_type->fixed_length + 1;
    char record_c[64];
    bool ok;

    loop_line(loop, 0, "{\n");
    append_indent(loop->source, loop->depth + 1);
    ok = format_c_type(loop->program, loop->iterable_node->type_index, record_c,
                       sizeof(record_c), loop->errors);
    buffer_appendf(loop->source, "%s nyx_for_src_%zu = ", record_c, o);
    ok = ok && emit_expression(loop->program, loop->node->left_child,
                               loop->source, loop->errors);
    buffer_append(loop->source, ";\n");
    loop_line(loop, 1,
              "for (size_t nyx_for_i_%zu = 0; nyx_for_i_%zu < %zu; "
              "nyx_for_i_%zu++) {\n",
              o, o, buckets, o);
    loop_line(loop, 2,
              "if (nyx_for_src_%zu.states[nyx_for_i_%zu] != 1) continue;\n", o,
              o);
    emit_map_variable(loop, map->key_c, loop->value,
                      "nyx_for_src_%zu.keys[nyx_for_i_%zu]", o, o);
    if (loop->index != NULL) {
        emit_map_variable(loop, map->value_c, loop->index,
                          "nyx_for_src_%zu.values[nyx_for_i_%zu]", o, o);
    }
    return ok;
}

/* A Table keeps them in its Store, found through its state. */
static bool emit_table_buckets(MapLoop *map)
{
    Loop *loop = map->loop;
    size_t o = loop->offset;
    size_t table_type = loop->iterable_node->type_index;
    const SourcePos *pos = &loop->node->pos;
    bool ok;

    loop_line(loop, 0, "{\n");
    loop_line(loop, 1, "nyx_table nyx_for_src_%zu = ", o);
    ok = emit_expression(loop->program, loop->node->left_child, loop->source,
                         loop->errors);
    buffer_append(loop->source, ";\n");
    loop_line(loop, 1,
              "for (size_t nyx_for_i_%zu = 0;; nyx_for_i_%zu++) {\n", o, o);
    loop_line(loop, 2,
              "nyx_table_state_%zu *nyx_for_state_%zu = "
              "nyx_table_%zu_state_at(nyx_for_src_%zu, __FILE__, %zu, %zu, "
              "%zu);\n",
              table_type, o, table_type, o, pos->line, pos->column,
              pos->offset);
    loop_line(loop, 2, "if (nyx_for_i_%zu >= nyx_for_state_%zu->buckets) break;\n",
              o, o);
    loop_line(loop, 2, "nyx_store *nyx_for_store_%zu = nyx_for_src_%zu.store;\n",
              o, o);
    loop_line(loop, 2,
              "uint8_t *nyx_for_states_%zu = (uint8_t *)(void *)"
              "(nyx_for_store_%zu->data + nyx_for_state_%zu->states_offset);\n"
              "        if (nyx_for_states_%zu[nyx_for_i_%zu] != 1) continue;\n",
              o, o, o, o, o);
    loop_line(loop, 2,
              "%s *nyx_for_keys_%zu = (%s *)(void *)(nyx_for_store_%zu->data + "
              "nyx_for_state_%zu->keys_offset);\n",
              map->key_c, o, map->key_c, o, o);
    emit_map_variable(loop, map->key_c, loop->value, "nyx_for_keys_%zu[nyx_for_i_%zu]",
                      o, o);
    if (loop->index != NULL) {
        loop_line(loop, 2,
                  "%s *nyx_for_values_%zu = (%s *)(void *)(nyx_for_store_%zu->data "
                  "+ nyx_for_state_%zu->values_offset);\n",
                  map->value_c, o, map->value_c, o, o);
        emit_map_variable(loop, map->value_c, loop->index,
                          "nyx_for_values_%zu[nyx_for_i_%zu]", o, o);
    }
    return ok;
}

static bool emit_map_for(Loop *loop)
{
    const TypedType *type = loop->iterable_type;
    MapLoop map = {loop, "", ""};
    bool ok;

    if (!format_c_type(loop->program,
                       *(const size_t *)list_get(&loop->program->type_children,
                                                 type->first_child),
                       map.key_c, sizeof(map.key_c), loop->errors) ||
        !format_c_type(loop->program,
                       *(const size_t *)list_get(&loop->program->type_children,
                                                 type->first_child + 1),
                       map.value_c, sizeof(map.value_c), loop->errors)) {
        return emit_unsupported_statement(loop->errors);
    }
    ok = type->kind == NYX_TYPE_RECORD ? emit_record_buckets(&map)
                                       : emit_table_buckets(&map);
    ok = ok && loop_finish(loop, 2);
    loop_close(loop);
    return ok;
}

/* ---- the statement ---- */

/* A loop variable's C name; NULL when there is none or it has no C spelling. */
static const char *loop_variable(const TypedProgram *program, size_t symbol,
                                 char *text, size_t size)
{
    return symbol == TYPED_NO_NODE ? NULL
                                   : user_c_name(program, symbol, text, size);
}

/* Loops over the C types they bind: the iterable's, and the element's. */
static bool format_loop_types(Loop *loop)
{
    return format_c_type(loop->program, loop->iterable_node->type_index,
                         loop->iterable_c, sizeof(loop->iterable_c),
                         loop->errors) &&
           format_c_type(loop->program, loop->node->auxiliary_type,
                         loop->element_c, sizeof(loop->element_c),
                         loop->errors);
}

/* The loop's variables: one, or two when the second names the value or the
   index. */
static bool bind_loop_variables(Loop *loop, char *value_text, char *index_text)
{
    const TypedNode *node = loop->node;

    loop->value = user_c_name(loop->program, node->name_index, value_text,
                              USER_NAME_CAPACITY);
    loop->index = loop_variable(loop->program, node->second_name_index,
                                index_text, USER_NAME_CAPACITY);
    return loop->value != NULL &&
           (node->second_name_index == TYPED_NO_NODE || loop->index != NULL);
}

bool emit_for_statement(const TypedProgram *program, GeneratorContext *gen,
                        size_t node_index, size_t depth,
                        const char *effective_source, TextBuffer *source,
                        ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, node_index);
    char value_text[USER_NAME_CAPACITY];
    char index_text[USER_NAME_CAPACITY];
    Loop loop = {program, gen, node, effective_source, source, errors, depth,
                 node->pos.offset, typed_program_node(program, node->left_child),
                 NULL, NULL, NULL, "", ""};
    NyxType kind;

    loop.iterable_type = loop.iterable_node == NULL
                             ? NULL
                             : typed_program_type(program,
                                                  loop.iterable_node->type_index);
    if (loop.iterable_type == NULL ||
        !bind_loop_variables(&loop, value_text, index_text)) {
        return emit_unsupported_statement(errors);
    }
    kind = loop.iterable_type->kind;
    if (kind == NYX_TYPE_RECORD || kind == NYX_TYPE_TABLE) {
        return emit_map_for(&loop);
    }
    if (!format_loop_types(&loop)) {
        return emit_unsupported_statement(errors);
    }
    return kind == NYX_TYPE_RANGE ? emit_range_for(&loop)
                                  : emit_sequence_for(&loop);
}
