#include "generator_internal.h"


/* The part of a pop helper that returns Nil for an empty list and otherwise
   removes the last element into `value`. */
static bool append_pop_prologue(TextBuffer *body, const char *union_name,
                                const char *element_name,
                                const char *list_name, size_t index,
                                bool fixed, size_t nil_tag)
{
    const char *params_format = fixed
        ? "%s *list"
        : "nyx_list list, const char *file, size_t line, size_t column, "
          "size_t pos";
    char params[NYX_IDENTIFIER_MAX + 96];

    snprintf(params, sizeof(params), params_format, list_name);
    buffer_appendf(body,
                    "static NYX_UNUSED %s nyx_list_pop_%zu(%s)\n{\n"
                    "    %s result;\n    %s value;\n\n"
                    "    if (%s == 0) {\n"
                    "        result = (%s){ .tag = %zu };\n"
                    "        return result;\n"
                    "    }\n",
                    union_name, index, params, union_name, element_name,
                    fixed ? "list->length"
                          : "nyx_list_get_state_at(list, file, line, "
                            "column, pos)->length",
                    union_name, nil_tag);
    if (fixed) {
        return buffer_append(body,
                             "    value = list->data[list->length - 1];\n"
                             "    list->length--;\n");
    }
    return buffer_appendf(
        body,
        "    {\n"
        "        nyx_list_state *state = "
        "nyx_list_get_state_at(list, file, line, column, pos);\n"
        "        nyx_store *store = "
        "nyx_list_store_at(list, file, line, column, pos);\n"
        "        size_t offset = state->offset + "
        "(state->length - 1) * sizeof(%s);\n\n"
        "        value = *(%s *)(void *)(store->data + offset);\n"
        "        state->length--;\n"
        "    }\n",
        element_name, element_name);
}

static void emit_list_runtime_dynamic_list(GeneratorContext *gen, bool *ok, TextBuffer *source)
{
    (void)ok;
    {
        if (analysis_has(gen->analysis, ANALYSIS_DYNAMIC_LIST)) {
            buffer_append(
                           source,
                           "typedef struct {\n"
                           "    size_t offset;\n"
                           "    size_t length;\n"
                           "    size_t capacity;\n"
                           "} nyx_list_state;\n\n"
                           "static NYX_UNUSED nyx_store *nyx_list_store_at("
                           "nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    if (list.store == NULL) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_STORE_RANGE "\", file, line, column, pos);\n"
                           "    }\n"
                           );
            append_generation_check(source, gen,
                                         "list.store->generation",
                                         "list.generation",
                                         "nyx_list_fail_at");
            buffer_append(
                           source,
                           "    return list.store;\n"
                           "}\n\n"
                           "static NYX_UNUSED nyx_store *nyx_list_store(nyx_list list)\n"
                           "{ return nyx_list_store_at(list, NULL, 0, 0, 0); }\n\n"
                           "static NYX_UNUSED nyx_list_state *"
                           "nyx_list_get_state_at(nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n\n"
                           "    if (list.offset > store->used ||\n"
                           "        sizeof(nyx_list_state) > "
                           "store->used - list.offset) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    return (nyx_list_state *)(void *)"
                           "(store->data + list.offset);\n"
                           "}\n\n"
                           "static NYX_UNUSED nyx_list_state *nyx_list_get_state(nyx_list list)\n"
                           "{ return nyx_list_get_state_at(list, NULL, 0, 0, 0); }\n\n"
                           "static NYX_UNUSED void *nyx_list_data_at("
                           "nyx_list list, int32_t index,\n"
                           "                                      "
                           "size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_list_state *state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "    nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n"
                           "    size_t offset;\n\n"
                           "    if (index < 0 || (size_t)index >= state->length ||\n"
                           "        element_size == 0) {\n"
                           "        nyx_list_fail_at(\"");
            buffer_append(
                           source,
                           MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    if ((size_t)index > (size_t)-1 / "
                           "element_size) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    offset = state->offset + (size_t)index * "
                           "element_size;\n"
                           "    if (offset > store->used ||\n"
                           "        element_size > store->used - offset) {\n"
                           "        nyx_list_fail_at(\"");
            buffer_append(
                           source,
                           MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    return (void *)(store->data + offset);\n"
                           "}\n\n"
                           "static NYX_UNUSED void *nyx_list_data(nyx_list list, int32_t index, size_t element_size)\n"
                           "{ return nyx_list_data_at(list, index, element_size, NULL, 0, 0, 0); }\n\n");
            buffer_append(
                           source,
                           "static NYX_UNUSED nyx_list nyx_list_create("
                           "nyx_store *store, int32_t capacity,\n"
                           "                                              "
                           "size_t element_size, size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_list_state state;\n"
                           "    nyx_handle handle;\n"
                           "    nyx_list list;\n"
                           "    size_t cap;\n"
                           "    size_t bytes;\n"
                           "    size_t buffer;\n\n"
                           "    if (capacity < 0) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_NEGATIVE_CAPACITY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    if (element_size == 0) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                           "    }\n"
                           "    cap = (size_t)capacity;\n"
                           "    if (cap > (size_t)-1 / element_size) {\n"
                           "        nyx_list_fail_at(\""
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    bytes = cap * element_size;\n"
                           "    buffer = nyx_store_alloc_bytes_aligned_at(store,\n"
                           "        bytes == 0 ? 1 : bytes, alignment, file, line, column, pos);\n"
                           "    state.offset = buffer;\n"
                           "    state.length = 0;\n"
                           "    state.capacity = cap;\n"
                           "    handle = nyx_store_alloc_at(store, &state, "
                           "sizeof(state),\n"
                           "                               "
                           "_Alignof(nyx_list_state), file, line, column, pos);\n"
                           "    list.store = store;\n"
                           "    list.offset = handle.offset;\n"
                           "    list.generation = handle.generation;\n"
                           "    return list;\n"
                           "}\n\n");
            buffer_append(
                           source,
                           "static NYX_UNUSED void nyx_list_push("
                           "nyx_list list, const void *value,\n"
                           "                                      "
                           "size_t element_size, size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    nyx_list_state *state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "    nyx_store *store;\n"
                           "    size_t needed;\n"
                           "    size_t capacity;\n\n"
                           "    if (state->length == (size_t)-1) {\n"
                           "        nyx_list_fail_at(\"");
            buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "    }\n"
                           "    needed = state->length + 1;\n"
                           "    if (needed > state->capacity) {\n"
                           "        size_t old_offset = state->offset;\n"
                           "        size_t old_length = state->length;\n"
                           "        size_t old_bytes = old_length * element_size;\n"
                           "        size_t new_bytes;\n"
                           "        size_t new_offset;\n\n"
                           "        capacity = state->capacity == 0 ? 1 : "
                           "state->capacity;\n"
                           "        while (capacity < needed) {\n"
                           "            if (capacity > (size_t)-1 / 2) {\n"
                           "                nyx_list_fail_at(\"");
            buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "            }\n"
                           "            capacity *= 2;\n"
                           "        }\n"
                           "        if (capacity > (size_t)-1 / element_size) {\n"
                           "            nyx_list_fail_at(\"");
            buffer_append(
                           source,
                           MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                           "        }\n"
                           "        new_bytes = capacity * element_size;\n"
                           "        new_offset = nyx_store_alloc_bytes_aligned_at(\n"
                           "            nyx_list_store_at(list, file, line, column, pos), "
                           "new_bytes,\n"
                           "            alignment, file, line, column, pos);\n"
                           "        store = nyx_list_store_at(list, file, line, column, pos);\n"
                           "        memcpy(store->data + new_offset, "
                           "store->data + old_offset,\n"
                           "               old_bytes);\n"
                           "        state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "        state->offset = new_offset;\n"
                           "        state->capacity = capacity;\n"
                           "    }\n"
                           "    state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                           "    store = nyx_list_store_at(list, file, line, column, pos);\n"
                           "    memcpy(store->data + state->offset + "
                           "state->length * element_size,\n"
                           "           value, element_size);\n"
                           "    state->length = needed;\n"
                           "}\n\n"
                           "static NYX_UNUSED int32_t nyx_list_len_at("
                           "nyx_list list, const char *file, size_t line, size_t column, size_t pos)\n"
                           "{\n"
                           "    return (int32_t)nyx_list_get_state_at(list, file, line, column, pos)->length;\n"
                           "}\n\n"
                           "static NYX_UNUSED int32_t nyx_list_len(nyx_list list)\n"
                           "{ return nyx_list_len_at(list, NULL, 0, 0, 0); }\n\n");
        }
    }
}

/* One optional read of a list, array or slice, as a helper in the C output:
   the element, or Nil when the index is out of range. */
typedef struct {
    const TypedProgram *program;
    GeneratorContext *gen;
    const TypedNode *node;
    size_t index;
    size_t nil_tag;
    char union_name[NYX_IDENTIFIER_MAX + 32];
    char element_name[NYX_IDENTIFIER_MAX + 32];
    char list_name[NYX_IDENTIFIER_MAX + 32];
} OptionalGet;

/* An Array is a value: the helper reads it directly. */
static void optional_prologue_array(const OptionalGet *g, TextBuffer *body)
{
    buffer_appendf(
        body,
        "static NYX_UNUSED %s nyx_list_get_optional_%zu(%s list, int32_t index)\n"
        "{\n"
        "    %s result;\n"
        "    %s value;\n\n"
        "    if (index < 0 || (size_t)index >= list.length) {\n"
        "        result = (%s){ .tag = %zu };\n"
        "        return result;\n"
        "    }\n"
        "    value = list.data[index];\n",
        g->union_name, g->index, g->list_name, g->union_name, g->element_name,
        g->union_name, g->nil_tag);
}

/* A List is found through its state in a Store. */
static void optional_prologue_list(const OptionalGet *g, TextBuffer *body)
{
    buffer_appendf(
        body,
        "static NYX_UNUSED %s nyx_list_get_optional_%zu("
        "nyx_list list, int32_t index, const char *file, size_t line, size_t column, size_t pos)\n"
        "{\n"
        "    %s result;\n"
        "    %s value;\n\n"
        "    {\n"
        "        nyx_list_state *state = "
        "nyx_list_get_state_at(list, file, line, column, pos);\n"
        "        nyx_store *store;\n"
        "        size_t offset;\n\n"
        "        if (index < 0 || "
        "(size_t)index >= state->length) {\n"
        "            result = (%s){ .tag = %zu };\n"
        "            return result;\n"
        "        }\n"
        "        store = nyx_list_store_at(list, file, line, column, pos);\n"
        "        offset = state->offset + "
        "(size_t)index * sizeof(%s);\n"
        "        if (offset > store->used ||\n"
        "            sizeof(%s) > store->used - offset) {\n"
        "            nyx_list_fail_at(\""
        MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
        "        }\n"
        "        value = *(%s *)(void *)"
        "(store->data + offset);\n"
        "    }\n",
        g->union_name, g->index, g->union_name, g->element_name, g->union_name,
        g->nil_tag, g->element_name, g->element_name, g->element_name);
}

/* A Slice is a window onto its List. */
static void optional_prologue_slice(const OptionalGet *g, TextBuffer *body)
{
    buffer_appendf(
        body,
        "static NYX_UNUSED %s nyx_slice_get_optional_%zu("
        "nyx_slice slice, int32_t index, const char *file, size_t line, size_t column, size_t pos)\n"
        "{\n"
        "    %s result;\n"
        "    %s value;\n"
        "    nyx_store *store = nyx_slice_store_at(slice, file, line, column, pos);\n"
        "    nyx_list_state *state = nyx_slice_window_at(slice, file, line, column, pos);\n"
        "    size_t offset;\n\n"
        "    if (index < 0 || (size_t)index >= slice.length) {\n"
        "        result = (%s){ .tag = %zu };\n"
        "        return result;\n"
        "    }\n"
        "    offset = state->offset + (slice.start + (size_t)index) * "
        "sizeof(%s);\n"
        "    if (offset > store->used ||\n"
        "        sizeof(%s) > store->used - offset) {\n"
        "        nyx_list_fail_at(\""
        MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
        "    }\n"
        "    value = *(%s *)(const void *)(store->data + offset);\n",
        g->union_name, g->index, g->union_name, g->element_name, g->union_name,
        g->nil_tag, g->element_name, g->element_name, g->element_name);
}

/* A read whose element type is Nil has nothing to return but Nil. */
static void emit_nil_optional_get(TextBuffer *source, size_t index, bool slice)
{
    buffer_appendf(
        source,
        slice ? "static NYX_UNUSED nyx_nil nyx_slice_get_optional_%zu("
                "nyx_slice slice, int32_t index, const char *file, size_t line, "
                "size_t column, size_t pos)\n{\n"
                "    (void)nyx_slice_store_at(slice, file, line, column, pos);\n"
                "    (void)index;\n"
                "    return (nyx_nil)0;\n}\n\n"
              : "static NYX_UNUSED nyx_nil nyx_list_get_optional_%zu(nyx_list "
                "list, int32_t index)\n{\n"
                "    (void)list;\n"
                "    (void)index;\n"
                "    return (nyx_nil)0;\n}\n\n",
        index);
}

static bool optional_get_names(OptionalGet *g, NyxType receiver,
                               ErrorState *errors)
{
    const TypedType *union_type = typed_program_type(g->program, g->node->type_index);

    return format_c_type(g->program, g->node->type_index, g->union_name,
                         sizeof(g->union_name), errors) &&
           format_c_type(g->program, g->node->auxiliary_type, g->element_name,
                         sizeof(g->element_name), errors) &&
           find_nil_tag(g->program, g->gen, union_type, &g->nil_tag) &&
           (receiver != NYX_TYPE_FIXED_LIST ||
            format_c_type(g->program,
                          typed_program_node(g->program, g->node->left_child)
                              ->type_index,
                          g->list_name, sizeof(g->list_name), errors));
}

static bool emit_optional_get(const TypedProgram *program, GeneratorContext *gen,
                              size_t index, NyxType receiver,
                              TextBuffer *source, ErrorState *errors)
{
    OptionalGet g = {program, gen, typed_program_node(program, index), index, 0,
                     "", "", ""};
    TextBuffer body;
    bool ok;

    if (!optional_get_names(&g, receiver, errors)) {
        return false;
    }
    buffer_init(&body);
    if (receiver == NYX_TYPE_SLICE) {
        optional_prologue_slice(&g, &body);
    } else if (receiver == NYX_TYPE_FIXED_LIST) {
        optional_prologue_array(&g, &body);
    } else {
        optional_prologue_list(&g, &body);
    }
    ok = append_optional_union(gen, &body, program, g.node->type_index,
                               g.node->auxiliary_type, g.union_name, "value",
                               "result");
    buffer_append(&body, "    return result;\n}\n\n");
    buffer_append_buffer(source, &body);
    buffer_free(&body);
    return ok;
}

/* One helper per optional read in the program: of Slices when `slices`, of
   Lists and Arrays otherwise. */
static bool emit_optional_gets(GeneratorContext *gen, const TypedProgram *program,
                               bool slices, TextBuffer *source,
                               ErrorState *errors)
{
    size_t count, k;
    const size_t *optionals = analysis_nodes(gen->analysis,
                                             TYPED_LIST_GET_OPTIONAL, &count);

    for (k = 0; k < count; k++) {
        const TypedNode *node = typed_program_node(program, optionals[k]);
        NyxType receiver = typed_program_node_kind(program, node->left_child);

        if ((receiver == NYX_TYPE_SLICE) != slices) {
            continue;
        }
        if (typed_program_type(program, node->type_index)->kind ==
            NYX_TYPE_NIL) {
            emit_nil_optional_get(source, optionals[k], slices);
        } else if (!emit_optional_get(program, gen, optionals[k], receiver,
                                      source, errors)) {
            return false;
        }
    }
    return true;
}

static bool emit_list_runtime_buffer_append(GeneratorContext *gen, bool *ok, TextBuffer *source, size_t *i, const TypedProgram *program, ErrorState *errors)
{
    (void)ok;
    buffer_append(
                   source,
                   "static NYX_UNUSED void nyx_list_fail("
                   "const char *message)\n"
                   "{\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n"
                   "static NYX_UNUSED void nyx_list_fail_at(const char *message, const char *file, size_t line, size_t column, size_t offset)\n"
                   "{\n"
                   "    if (file == NULL) nyx_list_fail(message);\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, offset, message + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
                   "    exit(1);\n"
                   "}\n\n");
    size_t fixed_count, k;
    const size_t *fixed_types = analysis_types(gen->analysis, NYX_TYPE_FIXED_LIST,
                                               &fixed_count);

    for (k = 0; k < fixed_count; k++) {
        const TypedType *type;
        char element_name[NYX_IDENTIFIER_MAX + 32];
        char list_name[NYX_IDENTIFIER_MAX + 32];
        size_t element;

        (*i) = fixed_types[k];
        type = typed_program_type(program, (*i));
        if (type == NULL || type->child_count != 1) {
            continue;
        }
        element = *(const size_t *)list_get(&program->type_children,
                                            type->first_child);
        if (!fixed_list_element_name(program, (*i), element, element_name,
                                     sizeof(element_name), errors)) {
            return false;
        }
        snprintf(list_name, sizeof(list_name), "nyx_list_%zu", (*i));
        buffer_appendf(
                       source,
                       "static NYX_UNUSED %s nyx_list_%zu_get("
                       "%s list, int32_t i, const char *file, size_t line, size_t column, size_t offset)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list.length) {\n"
                       "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\", file, line, column, offset);\n"
                       "    }\n"
                       "    return list.data[i];\n"
                       "}\n\n"
                       "static NYX_UNUSED void nyx_list_%zu_push("
                       "%s *list, %s value, const char *file, size_t line, size_t column, size_t offset)\n"
                       "{\n"
                       "    if (list->length %s %zu) {\n"
                       "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_ARRAY_FULL
                       "\", file, line, column, offset);\n"
                       "    }\n"
                       "    list->data[list->length] = value;\n"
                       "    list->length++;\n"
                       "}\n\n"
                       "static NYX_UNUSED %s *nyx_list_%zu_at("
                       "%s *list, int32_t i, const char *file, size_t line, size_t column, size_t offset)\n"
                       "{\n"
                       "    if (i < 0 || (size_t)i >= list->length) {\n"
                       "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS
                       "\", file, line, column, offset);\n"
                       "    }\n"
                       "    return &list->data[i];\n"
                       "}\n\n",
                       element_name, (*i), list_name,
                       (*i), list_name, element_name,
                       type->fixed_length == 0 ? "==" : ">=",
                       type->fixed_length,
                       element_name, (*i), list_name);
    }
    return true;
}

/* The helper behind one `pop`: its result is the element, or Nil. */
static bool emit_list_pop(const TypedProgram *program, GeneratorContext *gen, size_t index,
                          TextBuffer *source, ErrorState *errors)
{
    const TypedNode *node = typed_program_node(program, index);
    const TypedNode *receiver = typed_program_node(program, node->left_child);
    const TypedType *union_type = typed_program_type(program, node->type_index);
    char union_name[NYX_IDENTIFIER_MAX + 32];
    char element_name[NYX_IDENTIFIER_MAX + 32];
    char list_name[NYX_IDENTIFIER_MAX + 32];
    TextBuffer body;
    size_t nil_tag;
    bool ok;

    if (!format_c_type(program, node->type_index, union_name,
                       sizeof(union_name), errors) ||
        !format_c_type(program, node->auxiliary_type, element_name,
                       sizeof(element_name), errors) ||
        !format_c_type(program, receiver->type_index, list_name,
                       sizeof(list_name), errors) ||
        !find_nil_tag(program, gen, union_type, &nil_tag)) {
        return false;
    }
    buffer_init(&body);
    ok = append_pop_prologue(&body, union_name, element_name, list_name, index,
                             typed_program_type(program, receiver->type_index)
                                     ->kind == NYX_TYPE_FIXED_LIST,
                             nil_tag) &&
         append_optional_union(gen, &body, program, node->type_index,
                               node->auxiliary_type, union_name, "value",
                               "result");
    buffer_append(&body, "    return result;\n}\n\n");
    buffer_append_buffer(source, &body);
    buffer_free(&body);
    return ok;
}

bool emit_list_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                       ErrorState *errors)
{
    bool ok = true;
    size_t i;

    if (!emit_list_runtime_buffer_append(gen, &ok, source, &i, program, errors)) {
        return false;
    }
    emit_list_runtime_dynamic_list(gen, &ok, source);
    {
        size_t count;
        const size_t *pops = analysis_nodes(gen->analysis, TYPED_LIST_POP,
                                            &count);

        for (i = 0; i < count; i++) {
            if (!emit_list_pop(program, gen, pops[i], source, errors)) {
                return false;
            }
        }
    }
    if (!emit_optional_gets(gen, program, false, source, errors)) {
        return false;
    }
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


/* nyx_slice_store_at: the Store behind a slice, trapping on a missing one and,
   when a generation can change, on a stale one. */
static bool emit_slice_store_accessor(TextBuffer *source,
                                      GeneratorContext *gen)
{
    return buffer_append(
               source,
               "static NYX_UNUSED nyx_store *nyx_slice_store_at(nyx_slice slice, const char *file, size_t line, size_t column, size_t pos)\n"
               "{\n"
               "    if (slice.store == NULL) {\n"
               "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE "\", file, line, column, pos);\n"
               "    }\n") &&
           append_generation_check(source, gen, "slice.store->generation",
                                   "slice.generation", "nyx_list_fail_at") &&
           buffer_append(source,
                         "    return slice.store;\n"
                         "}\n");
}

/* The slice's Store accessor and the window onto its list's current state. */
static bool emit_slice_accessors(TextBuffer *source, GeneratorContext *gen)
{
    return emit_slice_store_accessor(source, gen) &&
           buffer_append(
                   source,
                   "\n"
                   "static NYX_UNUSED nyx_list_state *nyx_slice_window_at(nyx_slice slice, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store = nyx_slice_store_at(slice, file, line, column, pos);\n"
                   "    nyx_list_state *state;\n"
                   "\n"
                   "    if (slice.offset > store->used ||\n"
                   "        sizeof(nyx_list_state) > store->used - slice.offset) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    state = (nyx_list_state *)(void *)(store->data + slice.offset);\n"
                   "    if (slice.start > state->length ||\n"
                   "        slice.length > state->length - slice.start) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return state;\n"
                   "}\n"
                   "\n");
}

static bool emit_slice_runtime_i(GeneratorContext *gen, bool *ok, TextBuffer *source, const TypedProgram *program, ErrorState *errors)
{
    /* A slice is a window onto its list: it holds the list's state offset, the
       index of its first element, and its length, and finds the buffer through
       the list's current state on every access. Growing the list moves the
       buffer and the window follows; if the list has shrunk below the window,
       any use of the slice traps. It never reads a stale copy. */
    emit_slice_accessors(source, gen);
    buffer_append(
                   source,
                   "static NYX_UNUSED nyx_slice nyx_slice_from_list(nyx_list list, int32_t start, int32_t length,\n"
                   "                                                  size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_list_state *state = nyx_list_get_state_at(list, file, line, column, pos);\n"
                   "    nyx_store *store = nyx_list_store_at(list, file, line, column, pos);\n"
                   "    nyx_slice result;\n"
                   "\n"
                   "    (void)element_size;\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > state->length ||\n"
                   "        (size_t)length > state->length - (size_t)start) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    result.store = list.store;\n"
                   "    result.offset = list.offset;\n"
                   "    result.start = (size_t)start;\n"
                   "    result.length = (size_t)length;\n"
                   "    result.generation = store->generation;\n"
                   "    return result;\n"
                   "}\n"
                   "\n"
                   "static NYX_UNUSED nyx_slice nyx_slice_sub(nyx_slice slice, int32_t start, int32_t length,\n"
                   "                                            size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_slice result;\n"
                   "\n"
                   "    (void)element_size;\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > slice.length ||\n"
                   "        (size_t)length > slice.length - (size_t)start) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    (void)nyx_slice_window_at(slice, file, line, column, pos);\n"
                   "    result = slice;\n"
                   "    result.start += (size_t)start;\n"
                   "    result.length = (size_t)length;\n"
                   "    return result;\n"
                   "}\n"
                   "\n");
    buffer_append(
                   source,
                   "static NYX_UNUSED const void *nyx_slice_data_at(nyx_slice slice, int32_t index, size_t element_size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store = nyx_slice_store_at(slice, file, line, column, pos);\n"
                   "    nyx_list_state *state = nyx_slice_window_at(slice, file, line, column, pos);\n"
                   "    size_t position;\n"
                   "    size_t offset;\n"
                   "\n"
                   "    if (index < 0 || (size_t)index >= slice.length || element_size == 0) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    position = slice.start + (size_t)index;\n"
                   "    if (position > ((size_t)-1 - state->offset) / element_size) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_LIST_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    offset = state->offset + position * element_size;\n"
                   "    if (offset > store->used ||\n"
                   "        element_size > store->used - offset) {\n"
                   "        nyx_list_fail_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return (const void *)(store->data + offset);\n"
                   "}\n"
                   "\n"
                   "static NYX_UNUSED int32_t nyx_slice_len_at(nyx_slice slice, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    (void)nyx_slice_window_at(slice, file, line, column, pos);\n"
                   "    return (int32_t)slice.length;\n"
                   "}\n\n"
                   "static NYX_UNUSED const void *nyx_slice_data(nyx_slice slice, int32_t index, size_t size)\n"
                   "{ return nyx_slice_data_at(slice, index, size, NULL, 0, 0, 0); }\n\n"
                   "static NYX_UNUSED int32_t nyx_slice_len(nyx_slice slice)\n"
                   "{ return nyx_slice_len_at(slice, NULL, 0, 0, 0); }\n");
    (void)ok;
    return emit_optional_gets(gen, program, true, source, errors);
}

bool emit_slice_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source,
                        ErrorState *errors)
{
    bool ok = true;
    if (!emit_slice_runtime_i(gen, &ok, source, program, errors)) {
        return false;
    }
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
