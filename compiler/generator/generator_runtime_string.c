#include "generator_internal.h"


void emit_string_runtime(const TypedProgram *program, GeneratorContext *gen, TextBuffer *source)
{
    bool emitted_bounds = false;
    size_t i, k, count;
    const size_t *strings = analysis_types(gen->analysis, NYX_TYPE_STRING,
                                           &count);

    for (k = 0; k < count; k++) {
        const TypedType *type;
        bool already = false;
        size_t j;

        i = strings[k];
        type = typed_program_type(program, i);
        if (type == NULL) {
            continue;
        }
        /* One helper per capacity: skip a capacity an earlier type had. */
        for (j = 0; j < k; j++) {
            const TypedType *earlier = typed_program_type(program, strings[j]);

            if (earlier != NULL && earlier->fixed_length == type->fixed_length) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        if (!emitted_bounds) {
            buffer_append(
                source,
                "static inline void nyx_string_bounds(const char *file, size_t line, size_t column, size_t offset)\n"
                "{\n"
                "    fflush(stdout);\n"
                "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION
                "\\n\", file, line, column, offset, \""
                MESSAGE_RUNTIME_STRING_BOUNDS "\" + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
                "    exit(1);\n"
                "}\n\n");
            emitted_bounds = true;
        }
        buffer_appendf(
            source,
            "static inline uint8_t nyx_string_%zu_at(nyx_string_%zu s, "
            "int32_t i, const char *file, size_t line, size_t column, size_t offset)\n"
            "{\n"
            "    if (i < 0 || (uint32_t)i >= s.length) {\n"
            "        nyx_string_bounds(file, line, column, offset);\n"
            "    }\n"
            "    return s.data[i];\n"
            "}\n\n"
            "static inline bool nyx_string_%zu_equal(nyx_string_%zu a, "
            "nyx_string_%zu b)\n"
            "{\n"
            "    return a.length == b.length &&\n"
            "           memcmp(a.data, b.data, a.length) == 0;\n"
            "}\n\n",
            type->fixed_length, type->fixed_length, type->fixed_length,
            type->fixed_length, type->fixed_length);
    }
}


bool emit_dynamic_string_runtime(const TypedProgram *program, GeneratorContext *gen,
                                         TextBuffer *source,
                                         ErrorState *errors)
{
    bool ok = true;

    buffer_append(
                   source,
                   "static NYX_UNUSED void nyx_string_fail(const char *message)\n"
                   "{\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n"
                   "static NYX_UNUSED void nyx_string_fail_at(const char *message, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (file == NULL) nyx_string_fail(message);\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos, message + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
                   "    exit(1);\n"
                   "}\n\n");
    {
        buffer_append(
                       source,
                       "static NYX_UNUSED const uint8_t *"
                       "nyx_string_data_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    nyx_store *store;\n\n"
                       "    if (s.static_data != NULL) {\n"
                       "        return s.static_data;\n"
                       "    }\n"
                       "    if (s.store == NULL) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    store = s.store;\n"
                       );
        append_generation_check(source, gen, "store->generation",
                                     "s.generation", "nyx_string_fail_at");
        buffer_append(
                       source,
                       "    if (s.offset > store->used ||\n"
                       "        s.length > store->used - s.offset ||\n"
                       "        s.length > (size_t)-1 - s.offset - 1 ||\n"
                       "        s.offset + s.length + 1 > store->used) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    return store->data + s.offset;\n"
                       "}\n\n");
    }
    buffer_append(
                   source,
                   "static NYX_UNUSED const uint8_t *nyx_string_data(nyx_string s)\n"
                   "{ return nyx_string_data_at(s, NULL, 0, 0, 0); }\n\n"
                   "static NYX_UNUSED int32_t nyx_string_len_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    (void)nyx_string_data_at(s, file, line, column, pos);\n"
                   "    return (int32_t)s.length;\n"
                   "}\n\n"
                   "static NYX_UNUSED uint8_t nyx_string_byte_at(nyx_string s, "
                   "int32_t i, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (i < 0 || (size_t)i >= s.length) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return nyx_string_data_at(s, file, line, column, pos)[i];\n"
                   "}\n\n"
                   "static NYX_UNUSED nyx_string "
                   "nyx_string_slice(nyx_string s, "
                   "int32_t start, int32_t length, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_string result;\n\n"
                   "    if (start < 0 || length < 0 ||\n"
                   "        (size_t)start > s.length ||\n"
                   "        (size_t)length > s.length - (size_t)start) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    (void)nyx_string_data_at(s, file, line, column, pos);\n"
                   "    result = s;\n"
                   "    if (result.static_data != NULL) {\n"
                   "        result.static_data += start;\n"
                   "    } else {\n"
                   "        result.offset += (size_t)start;\n"
                   "    }\n"
                   "    result.length = (size_t)length;\n"
                   "    return result;\n"
                   "}\n\n"
                   "static NYX_UNUSED bool nyx_string_equal_at(nyx_string a, "
                   "nyx_string b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    const uint8_t *a_data = nyx_string_data_at(a, file, line, column, pos);\n"
                   "    const uint8_t *b_data = nyx_string_data_at(b, file, line, column, pos);\n"
                   "    return a.length == b.length && memcmp(a_data, b_data, a.length) == 0;\n"
                   "}\n\n"
                   "static NYX_UNUSED bool nyx_string_equal(nyx_string a, nyx_string b)\n"
                   "{\n"
                   "    return nyx_string_equal_at(a, b, NULL, 0, 0, 0);\n"
                   "}\n\n");
    {
        buffer_append(
                       source,
                       "static NYX_UNUSED nyx_string nyx_string_from_bytes("
                       "nyx_store *store,\n"
                       "                                             "
                       "const uint8_t *bytes, size_t length, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n"
                       "    nyx_string result;\n\n"
                       "    if (length > (size_t)-1 - 1) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    offset = nyx_store_alloc_bytes_at(store, "
                       "length + 1, file, line, column, pos);\n"
                       "    memcpy(store->data + offset, bytes, length);\n"
                       "    store->data[offset + length] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = store;\n"
                       "    result.offset = offset;\n"
                       "    result.length = length;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n"
                       "static NYX_UNUSED nyx_string nyx_string_concat("
                       "nyx_store *store,\n"
                       "                                        "
                       "nyx_string a, nyx_string b, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t total;\n"
                       "    size_t offset;\n"
                       "    nyx_string result;\n"
                       "    const uint8_t *a_data;\n"
                       "    const uint8_t *b_data;\n\n"
                       "    if (a.length > (size_t)-1 - 1 ||\n"
                       "        b.length > (size_t)-1 - 1 - a.length) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    total = a.length + b.length;\n"
                       "    offset = nyx_store_alloc_bytes_at(store, "
                       "total + 1, file, line, column, pos);\n"
                       "    a_data = nyx_string_data_at(a, file, line, column, pos);\n"
                       "    b_data = nyx_string_data_at(b, file, line, column, pos);\n"
                       "    memcpy(store->data + offset, a_data, a.length);\n"
                       "    memcpy(store->data + offset + a.length, b_data, "
                       "b.length);\n"
                       "    store->data[offset + total] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = store;\n"
                       "    result.offset = offset;\n"
                       "    result.length = total;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n"
                       "static NYX_UNUSED nyx_string nyx_string_copy("
                       "nyx_store *store,\n"
                       "                                      nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n"
                       "    nyx_string result;\n"
                       "    const uint8_t *data;\n\n"
                       "    if (s.length > (size_t)-1 - 1) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    offset = nyx_store_alloc_bytes_at(store, "
                       "s.length + 1, file, line, column, pos);\n"
                       "    data = nyx_string_data_at(s, file, line, column, pos);\n"
                       "    memcpy(store->data + offset, data, s.length);\n"
                       "    store->data[offset + s.length] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = store;\n"
                       "    result.offset = offset;\n"
                       "    result.length = s.length;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n");
        {
            size_t i, k, count;
            const size_t *strings = analysis_types(gen->analysis,
                                                   NYX_TYPE_STRING, &count);

            for (k = 0; k < count; k++) {
                const TypedType *type;
                bool already = false;
                size_t j;

                i = strings[k];
                type = typed_program_type(program, i);
                if (type == NULL) {
                    continue;
                }
                for (j = 0; j < k; j++) {
                    const TypedType *earlier = typed_program_type(program,
                                                                  strings[j]);

                    if (earlier != NULL &&
                        earlier->fixed_length == type->fixed_length) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    continue;
                }
                buffer_appendf(
                               source,
                               "static NYX_UNUSED nyx_string "
                               "nyx_string_from_fixed_%zu("
                               "nyx_store *store,\n"
                               "                                             "
                               "nyx_string_%zu s, const char *file, size_t line, size_t column, size_t pos)\n"
                               "{\n"
                               "    return nyx_string_from_bytes(store, "
                               "s.data, s.length, file, line, column, pos);\n"
                               "}\n\n",
                               type->fixed_length, type->fixed_length);
            }
        }
    }
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


bool emit_dynamic_string_literals(const TypedProgram *program, GeneratorContext *gen,
                                         TextBuffer *source)
{
    size_t count, k;
    const size_t *literals = analysis_nodes(gen->analysis, TYPED_STRING_LITERAL,
                                            &count);

    for (k = 0; k < count; k++) {
        size_t i = literals[k];
        const TypedNode *node = typed_program_node(program, i);
        const TypedType *type;
        size_t j;

        type = typed_program_type(program, node->type_index);
        if (type == NULL || type->kind != NYX_TYPE_DYNAMIC_STRING) {
            continue;
        }
        buffer_appendf(source,
                        "static const uint8_t nyx_dyn_str_%zu[] = { ",
                        i);
        for (j = 0; j < node->string_length; j++) {
            const unsigned char *byte = list_get(&program->bytes,
                                                 node->string_offset + j);

            if (byte == NULL) {
                return false;
            }
            buffer_appendf(source, "%u, ", (unsigned)*byte);
        }
        buffer_append(source, "0 };\n");
    }
    return true;
}


bool emit_builder_runtime(GeneratorContext *gen, TextBuffer *source, ErrorState *errors)
{
    bool ok = true;

    buffer_append(
                   source,
                   "typedef struct {\n"
                   "    size_t offset;\n"
                   "    size_t length;\n"
                   "    size_t capacity;\n"
                   "} nyx_builder_state;\n\n"
                   "static NYX_UNUSED nyx_store *nyx_builder_store("
                   "nyx_builder b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (b.store == NULL) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   );
    append_generation_check(source, gen, "b.store->generation",
                                 "b.generation", "nyx_string_fail_at");
    buffer_append(
                   source,
                   "    return b.store;\n"
                   "}\n\n"
                   "static NYX_UNUSED nyx_builder_state *nyx_builder_get_state("
                   "nyx_builder b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store = nyx_builder_store(b, file, line, column, pos);\n\n"
                   "    if (b.offset > store->used ||\n"
                   "        sizeof(nyx_builder_state) > store->used - b.offset) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return (nyx_builder_state *)(void *)(store->data + "
                   "b.offset);\n"
                   "}\n\n");
    buffer_append(
                   source,
                   "static NYX_UNUSED nyx_builder nyx_builder_create("
                   "nyx_store *store, int32_t capacity, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_builder_state state;\n"
                   "    nyx_handle handle;\n"
                   "    nyx_builder builder;\n"
                   "    size_t cap;\n"
                   "    size_t buffer;\n\n"
                   "    if (capacity < 0) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    cap = (size_t)capacity;\n"
                   "    if (cap > (size_t)-1 - 1) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    buffer = nyx_store_alloc_bytes_at(store, cap + 1, file, line, column, pos);\n"
                   "    store->data[buffer] = 0;\n"
                   "    state.offset = buffer;\n"
                   "    state.length = 0;\n"
                   "    state.capacity = cap;\n"
                   "    handle = nyx_store_alloc_at(store, &state, sizeof(state),\n"
                   "                               _Alignof(nyx_builder_state), file, line, column, pos);\n"
                   "    builder.store = store;\n"
                   "    builder.offset = handle.offset;\n"
                   "    builder.generation = handle.generation;\n"
                   "    return builder;\n"
                   "}\n\n"
                   "static NYX_UNUSED void nyx_builder_reserve("
                   "nyx_builder b, size_t needed, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store = nyx_builder_store(b, file, line, column, pos);\n"
                   "    nyx_builder_state *state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    size_t capacity;\n"
                   "    size_t old_offset;\n"
                   "    size_t old_length;\n"
                   "    size_t offset;\n\n"
                   "    if (needed <= state->capacity) {\n"
                   "        return;\n"
                   "    }\n"
                   "    capacity = state->capacity == 0 ? 1 : state->capacity;\n"
                   "    while (capacity < needed) {\n"
                   "        if (capacity > (size_t)-1 / 2) {\n"
                   "            nyx_string_fail_at(\""
                   MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                   "        }\n"
                   "        capacity *= 2;\n"
                   "    }\n"
                   "    old_offset = state->offset;\n"
                   "    old_length = state->length;\n"
                   "    offset = nyx_store_alloc_bytes_at(store, "
                   "capacity + 1, file, line, column, pos);\n"
                   "    state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    memcpy(store->data + offset, store->data + old_offset,\n"
                   "           old_length);\n"
                   "    state->offset = offset;\n"
                   "    state->capacity = capacity;\n"
                   "    state->length = old_length;\n"
                   "    store->data[offset + old_length] = 0;\n"
                   "}\n\n");
    buffer_append(
                   source,
                   "static NYX_UNUSED void nyx_builder_append("
                   "nyx_builder b, nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_builder_state *state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    size_t needed;\n"
                   "    const uint8_t *data;\n"
                   "    nyx_store *store;\n\n"
                   "    if (s.length > (size_t)-1 - state->length) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    needed = state->length + s.length;\n"
                   "    nyx_builder_reserve(b, needed, file, line, column, pos);\n"
                   "    state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    store = nyx_builder_store(b, file, line, column, pos);\n"
                   "    data = nyx_string_data_at(s, file, line, column, pos);\n"
                   "    memcpy(store->data + state->offset + state->length, data,\n"
                   "           s.length);\n"
                   "    state->length = needed;\n"
                   "    store->data[state->offset + needed] = 0;\n"
                   "}\n\n"
                   "static NYX_UNUSED void nyx_builder_append_byte("
                   "nyx_builder b, uint8_t byte, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_builder_state *state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    nyx_store *store;\n"
                   "    size_t needed;\n\n"
                   "    if (state->length == (size_t)-1) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    needed = state->length + 1;\n"
                   "    nyx_builder_reserve(b, needed, file, line, column, pos);\n"
                   "    state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    store = nyx_builder_store(b, file, line, column, pos);\n"
                   "    store->data[state->offset + state->length] = byte;\n"
                   "    state->length = needed;\n"
                   "    store->data[state->offset + needed] = 0;\n"
                   "}\n\n");
    buffer_append(
                   source,
                   "static NYX_UNUSED int32_t nyx_builder_len("
                   "nyx_builder b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    return (int32_t)nyx_builder_get_state(b, file, line, column, pos)->length;\n"
                   "}\n\n"
                   "static NYX_UNUSED nyx_string nyx_builder_finish("
                   "nyx_builder b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_builder_state *state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    nyx_store *store = nyx_builder_store(b, file, line, column, pos);\n"
                   "    nyx_string result;\n"
                   "    size_t length = state->length;\n"
                   "    size_t source = state->offset;\n"
                   "    size_t offset;\n\n"
                   "    if (length > (size_t)-1 - 1) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    offset = nyx_store_alloc_bytes_at(store, "
                   "length + 1, file, line, column, pos);\n"
                   "    memcpy(store->data + offset, store->data + source, "
                   "length);\n"
                   "    store->data[offset + length] = 0;\n"
                   "    result.static_data = NULL;\n"
                   "    result.store = b.store;\n"
                   "    result.offset = offset;\n"
                   "    result.length = length;\n"
                   "    result.generation = store->generation;\n"
                   "    return result;\n"
                   "}\n\n");
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
