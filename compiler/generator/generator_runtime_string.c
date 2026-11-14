#include "generator_internal.h"


bool emit_string_runtime(const TypedProgram *program, TextBuffer *source,
                                ErrorState *errors)
{
    bool emitted_bounds = false;
    size_t i;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);
        bool already = false;
        size_t j;

        if (type == NULL || type->kind != NIPPY_TYPE_STRING) {
            continue;
        }
        for (j = 0; j < i; j++) {
            const TypedType *earlier = typed_program_type(program, j);

            if (earlier != NULL && earlier->kind == NIPPY_TYPE_STRING &&
                earlier->fixed_length == type->fixed_length) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        if (!emitted_bounds) {
            if (!buffer_append(
                    source,
                    "static inline void nyx_string_bounds(const char *file, size_t line, size_t column, size_t offset)\n"
                    "{\n"
                    "    fflush(stdout);\n"
                    "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION
                    "\\n\", file, line, column, offset, \""
                    MESSAGE_RUNTIME_STRING_BOUNDS "\" + 7);\n"
                    "    exit(1);\n"
                    "}\n\n")) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
            emitted_bounds = true;
        }
        if (!buffer_appendf(
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
                type->fixed_length, type->fixed_length)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


bool emit_dynamic_string_runtime(const TypedProgram *program,
                                         size_t store_count,
                                         TextBuffer *source,
                                         ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED void nyx_string_fail(const char *message)\n"
                   "{\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n"
                   "static NIPPY_UNUSED void nyx_string_fail_at(const char *message, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (file == NULL) nyx_string_fail(message);\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos, message + 7);\n"
                   "    exit(1);\n"
                   "}\n\n");
    if (store_count > 0) {
        ok = ok && buffer_append(
                       source,
                       "static NIPPY_UNUSED const uint8_t *"
                       "nyx_string_data_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    nyx_store *store;\n\n"
                       "    if (s.static_data != NULL) {\n"
                       "        return s.static_data;\n"
                       "    }\n"
                       "    if (s.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    store = &nyx_stores[s.store];\n"
                       "    if (store->generation != s.generation) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STALE_HANDLE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    if (s.offset > store->used ||\n"
                       "        s.length > store->used - s.offset ||\n"
                       "        s.length > (size_t)-1 - s.offset - 1 ||\n"
                       "        s.offset + s.length + 1 > store->used) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    return store->data + s.offset;\n"
                       "}\n\n");
    } else {
        ok = ok && buffer_append(
                       source,
                       "static NIPPY_UNUSED const uint8_t *"
                       "nyx_string_data_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    if (s.static_data != NULL) {\n"
                       "        return s.static_data;\n"
                       "    }\n"
                       "    nyx_string_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    return NULL;\n"
                       "}\n\n");
    }
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED const uint8_t *nyx_string_data(nyx_string s)\n"
                   "{ return nyx_string_data_at(s, NULL, 0, 0, 0); }\n\n"
                   "static NIPPY_UNUSED int32_t nyx_string_len_at(nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    (void)nyx_string_data_at(s, file, line, column, pos);\n"
                   "    return (int32_t)s.length;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED uint8_t nyx_string_byte_at(nyx_string s, "
                   "int32_t i, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (i < 0 || (size_t)i >= s.length) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STRING_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return nyx_string_data_at(s, file, line, column, pos)[i];\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nyx_string "
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
                   "static NIPPY_UNUSED bool nyx_string_equal_at(nyx_string a, "
                   "nyx_string b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    const uint8_t *a_data = nyx_string_data_at(a, file, line, column, pos);\n"
                   "    const uint8_t *b_data = nyx_string_data_at(b, file, line, column, pos);\n"
                   "    return a.length == b.length && memcmp(a_data, b_data, a.length) == 0;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED bool nyx_string_equal(nyx_string a, nyx_string b)\n"
                   "{\n"
                   "    return nyx_string_equal_at(a, b, NULL, 0, 0, 0);\n"
                   "}\n\n");
    if (store_count > 0) {
        ok = ok && buffer_append(
                       source,
                       "static NIPPY_UNUSED nyx_string nyx_string_from_bytes("
                       "nyx_store *store, uint32_t id,\n"
                       "                                             "
                       "const uint8_t *bytes, size_t length, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n"
                       "    nyx_string result;\n\n"
                       "    if (length > (size_t)-1 - 1) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    offset = nyx_store_alloc_bytes_at(store, id, "
                       "length + 1, file, line, column, pos);\n"
                       "    memcpy(store->data + offset, bytes, length);\n"
                       "    store->data[offset + length] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = id;\n"
                       "    result.offset = offset;\n"
                       "    result.length = length;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED nyx_string nyx_string_concat("
                       "nyx_store *store, uint32_t id,\n"
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
                       "    offset = nyx_store_alloc_bytes_at(store, id, "
                       "total + 1, file, line, column, pos);\n"
                       "    a_data = nyx_string_data_at(a, file, line, column, pos);\n"
                       "    b_data = nyx_string_data_at(b, file, line, column, pos);\n"
                       "    memcpy(store->data + offset, a_data, a.length);\n"
                       "    memcpy(store->data + offset + a.length, b_data, "
                       "b.length);\n"
                       "    store->data[offset + total] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = id;\n"
                       "    result.offset = offset;\n"
                       "    result.length = total;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n"
                       "static NIPPY_UNUSED nyx_string nyx_string_copy("
                       "nyx_store *store, uint32_t id,\n"
                       "                                      nyx_string s, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n"
                       "    nyx_string result;\n"
                       "    const uint8_t *data;\n\n"
                       "    if (s.length > (size_t)-1 - 1) {\n"
                       "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    offset = nyx_store_alloc_bytes_at(store, id, "
                       "s.length + 1, file, line, column, pos);\n"
                       "    data = nyx_string_data_at(s, file, line, column, pos);\n"
                       "    memcpy(store->data + offset, data, s.length);\n"
                       "    store->data[offset + s.length] = 0;\n"
                       "    result.static_data = NULL;\n"
                       "    result.store = id;\n"
                       "    result.offset = offset;\n"
                       "    result.length = s.length;\n"
                       "    result.generation = store->generation;\n"
                       "    return result;\n"
                       "}\n\n");
        {
            size_t i;

            for (i = 0; i < program->types.length; i++) {
                const TypedType *type = typed_program_type(program, i);
                bool already = false;
                size_t j;

                if (type == NULL || type->kind != NIPPY_TYPE_STRING) {
                    continue;
                }
                for (j = 0; j < i; j++) {
                    const TypedType *earlier = typed_program_type(program, j);

                    if (earlier != NULL &&
                        earlier->kind == NIPPY_TYPE_STRING &&
                        earlier->fixed_length == type->fixed_length) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    continue;
                }
                ok = ok && buffer_appendf(
                               source,
                               "static NIPPY_UNUSED nyx_string "
                               "nyx_string_from_fixed_%zu("
                               "nyx_store *store, uint32_t id,\n"
                               "                                             "
                               "nyx_string_%zu s, const char *file, size_t line, size_t column, size_t pos)\n"
                               "{\n"
                               "    return nyx_string_from_bytes(store, id, "
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


bool emit_dynamic_string_literals(const TypedProgram *program,
                                         TextBuffer *source,
                                         ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->nodes.length; i++) {
        const TypedNode *node = typed_program_node(program, i);
        const TypedType *type;
        size_t j;

        if (node == NULL || node->kind != TYPED_STRING_LITERAL) {
            continue;
        }
        type = typed_program_type(program, node->type_index);
        if (type == NULL || type->kind != NIPPY_TYPE_DYNAMIC_STRING) {
            continue;
        }
        if (!buffer_appendf(source,
                            "static const uint8_t nyx_dyn_str_%zu[] = { ",
                            i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        for (j = 0; j < node->string_length; j++) {
            const unsigned char *byte = list_get(&program->bytes,
                                                 node->string_offset + j);

            if (byte == NULL ||
                !buffer_appendf(source, "%u, ", (unsigned)*byte)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                return false;
            }
        }
        if (!buffer_append(source, "0 };\n")) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    return true;
}


bool emit_builder_runtime(TextBuffer *source, ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_append(
                   source,
                   "typedef struct {\n"
                   "    size_t offset;\n"
                   "    size_t length;\n"
                   "    size_t capacity;\n"
                   "} nyx_builder_state;\n\n"
                   "static NIPPY_UNUSED nyx_store *nyx_builder_store("
                   "nyx_builder b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (b.store >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    if (nyx_stores[b.store].generation != b.generation) {\n"
                   "        nyx_string_fail_at(\"" MESSAGE_RUNTIME_STALE_HANDLE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return &nyx_stores[b.store];\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nyx_builder_state *nyx_builder_get_state("
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
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED nyx_builder nyx_builder_create("
                   "nyx_store *store, uint32_t id, int32_t capacity, const char *file, size_t line, size_t column, size_t pos)\n"
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
                   "    buffer = nyx_store_alloc_bytes_at(store, id, cap + 1, file, line, column, pos);\n"
                   "    store->data[buffer] = 0;\n"
                   "    state.offset = buffer;\n"
                   "    state.length = 0;\n"
                   "    state.capacity = cap;\n"
                   "    handle = nyx_store_alloc_at(store, id, &state, sizeof(state),\n"
                   "                               _Alignof(nyx_builder_state), file, line, column, pos);\n"
                   "    builder.store = id;\n"
                   "    builder.offset = handle.offset;\n"
                   "    builder.generation = handle.generation;\n"
                   "    return builder;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED void nyx_builder_reserve("
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
                   "    offset = nyx_store_alloc_bytes_at(store, b.store, "
                   "capacity + 1, file, line, column, pos);\n"
                   "    state = nyx_builder_get_state(b, file, line, column, pos);\n"
                   "    memcpy(store->data + offset, store->data + old_offset,\n"
                   "           old_length);\n"
                   "    state->offset = offset;\n"
                   "    state->capacity = capacity;\n"
                   "    state->length = old_length;\n"
                   "    store->data[offset + old_length] = 0;\n"
                   "}\n\n");
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED void nyx_builder_append("
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
                   "static NIPPY_UNUSED void nyx_builder_append_byte("
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
    ok = ok && buffer_append(
                   source,
                   "static NIPPY_UNUSED int32_t nyx_builder_len("
                   "nyx_builder b, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    return (int32_t)nyx_builder_get_state(b, file, line, column, pos)->length;\n"
                   "}\n\n"
                   "static NIPPY_UNUSED nyx_string nyx_builder_finish("
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
                   "    offset = nyx_store_alloc_bytes_at(store, b.store, "
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
