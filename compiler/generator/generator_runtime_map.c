#include "generator_internal.h"


bool emit_record_runtime(const TypedProgram *program, size_t type_index,
                                const TypedType *type, TextBuffer *source,
                                ErrorState *errors)
{
    size_t key_type = *(const size_t *)list_get(&program->type_children,
                                                type->first_child);
    size_t value_type = *(const size_t *)list_get(
        &program->type_children, type->first_child + 1);
    char key_name[64];
    char value_name[64];
    char record_name[64];
    size_t capacity = type->fixed_length;
    size_t buckets = capacity == 0 ? 1 : capacity + 1;

    if (!map_key_names(program, key_type, value_type, key_name,
                       sizeof(key_name), value_name, sizeof(value_name),
                       errors) ||
        !format_c_type(program, type_index, record_name, sizeof(record_name),
                       errors)) {
        return false;
    }
    if (!buffer_appendf(source,
        "static inline uint64_t nyx_record_%zu_hash(%s key)\n"
        "{\n"
        "    uint64_t h = 14695981039346656037ULL;\n"
        "    for (size_t i = 0; i < key.length; i++) {\n"
        "        h ^= key.data[i];\n"
        "        h *= 1099511628211ULL;\n"
        "    }\n"
        "    return h;\n"
        "}\n\n"
        "static inline bool nyx_record_%zu_eq(%s a, %s b)\n"
        "{\n"
        "    return a.length == b.length &&\n"
        "           memcmp(a.data, b.data, a.length) == 0;\n"
        "}\n\n"
        "static inline size_t nyx_record_%zu_find(const %s *r, %s key)\n"
        "{\n"
        "    size_t start = (size_t)(nyx_record_%zu_hash(key) %% %zu);\n"
        "    for (size_t p = 0; p < %zu; p++) {\n"
        "        size_t i = (start + p) %% %zu;\n"
        "        if (r->states[i] == 0) return SIZE_MAX;\n"
        "        if (r->states[i] == 1 && nyx_record_%zu_eq(r->keys[i], key)) "
        "return i;\n"
        "    }\n"
        "    return SIZE_MAX;\n"
        "}\n\n"
        "static inline bool nyx_record_%zu_get(const %s *r, %s key, %s *out)\n"
        "{\n"
        "    size_t i = nyx_record_%zu_find(r, key);\n"
        "    if (i == SIZE_MAX) return false;\n"
        "    *out = r->values[i];\n"
        "    return true;\n"
        "}\n\n"
        "static inline bool nyx_record_%zu_has(const %s *r, %s key)\n"
        "{\n"
        "    return nyx_record_%zu_find(r, key) != SIZE_MAX;\n"
        "}\n\n"
        "static inline int32_t nyx_record_%zu_len(const %s *r)\n"
        "{\n"
        "    return (int32_t)r->length;\n"
        "}\n\n"
        "static inline void nyx_record_%zu_set(%s *r, %s key, %s value, const char *file, size_t line, size_t column, size_t pos)\n"
        "{\n"
        "    size_t i = nyx_record_%zu_find(r, key);\n"
        "    if (i != SIZE_MAX) { r->values[i] = value; return; }\n"
        "    if (r->length %s %zu) nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
        "    size_t start = (size_t)(nyx_record_%zu_hash(key) %% %zu);\n"
        "    for (size_t p = 0; p < %zu; p++) {\n"
        "        size_t j = (start + p) %% %zu;\n"
        "        if (r->states[j] != 1) {\n"
        "            r->keys[j] = key;\n"
        "            r->values[j] = value;\n"
        "            r->states[j] = 1;\n"
        "            r->length++;\n"
        "            return;\n"
        "        }\n"
        "    }\n"
        "    nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
        "}\n\n"
        "static inline bool nyx_record_%zu_remove(%s *r, %s key)\n"
        "{\n"
        "    size_t i = nyx_record_%zu_find(r, key);\n"
        "    if (i == SIZE_MAX) return false;\n"
        "    r->states[i] = 2;\n"
        "    r->length--;\n"
        "    return true;\n"
        "}\n\n"
        "static inline %s nyx_record_%zu_get_required(const %s *r, %s key, const char *file, size_t line, size_t column, size_t pos)\n"
        "{\n"
        "    %s out;\n"
        "    if (!nyx_record_%zu_get(r, key, &out)) "
        "nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
        "    return out;\n"
        "}\n\n"
        "static inline %s nyx_record_%zu_literal(%s *keys, %s *values, size_t n, const char *file, size_t line, size_t column, size_t pos)\n"
        "{\n"
        "    %s r = {0};\n"
        "    for (size_t i = 0; i < n; i++) "
        "nyx_record_%zu_set(&r, keys[i], values[i], file, line, column, pos);\n"
        "    return r;\n"
        "}\n\n",
        type_index, key_name,
        type_index, key_name, key_name,
        type_index, record_name, key_name,
        type_index, buckets, buckets, buckets, type_index,
        type_index, record_name, key_name, value_name, type_index,
        type_index, record_name, key_name, type_index,
        type_index, record_name,
        type_index, record_name, key_name, value_name, type_index,
        capacity == 0 ? "==" : ">=", capacity, MESSAGE_RUNTIME_RECORD_FULL,
        type_index, buckets, buckets, buckets,
        MESSAGE_RUNTIME_RECORD_FULL,
        type_index, record_name, key_name, type_index,
        value_name, type_index, record_name, key_name,
        value_name, type_index, MESSAGE_RUNTIME_MAP_MISSING,
        record_name, type_index, key_name, value_name,
        record_name, type_index)) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}


bool emit_map_runtime(const TypedProgram *program, size_t store_count,
                             TextBuffer *source, ErrorState *errors)
{
    size_t i;
    bool map_helpers = false;

    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type != NULL && (type->kind == NIPPY_TYPE_RECORD ||
                             type->kind == NIPPY_TYPE_TABLE)) {
            map_helpers = true;
        }
    }
    if (!map_helpers) {
        return true;
    }
    if (!buffer_append(source,
        "static inline void nyx_map_fail(const char *message)\n"
        "{\n"
        "    fflush(stdout);\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n\n"
        "static inline void nyx_map_fail_at(const char *message, const char *file, size_t line, size_t column, size_t pos)\n"
        "{\n"
        "    if (file == NULL) nyx_map_fail(message);\n"
        "    fflush(stdout);\n"
        "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos, message + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
        "    exit(1);\n"
        "}\n\n")) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    (void)store_count;
    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);

        if (type == NULL || type->kind != NIPPY_TYPE_RECORD) {
            continue;
        }
        if (!emit_record_runtime(program, i, type, source, errors)) {
            return false;
        }
    }
    for (i = 0; i < program->types.length; i++) {
        const TypedType *type = typed_program_type(program, i);
        size_t key_type;
        size_t value_type;
        char key_name[64];
        char value_name[64];

        if (type == NULL || type->kind != NIPPY_TYPE_TABLE) {
            continue;
        }
        key_type = *(const size_t *)list_get(&program->type_children,
                                             type->first_child);
        value_type = *(const size_t *)list_get(&program->type_children,
                                               type->first_child + 1);
        if (!map_key_names(program, key_type, value_type, key_name,
                           sizeof(key_name), value_name, sizeof(value_name),
                           errors)) {
            return false;
        }
        if (!buffer_appendf(source,
            "typedef struct {\n"
            "    size_t buckets;\n"
            "    size_t length;\n"
            "    size_t capacity;\n"
            "    size_t keys_offset;\n"
            "    size_t values_offset;\n"
            "    size_t states_offset;\n"
            "} nyx_table_state_%zu;\n\n"
            "static inline uint64_t nyx_table_%zu_hash(%s key)\n"
            "{\n"
            "    uint64_t h = 14695981039346656037ULL;\n"
            "    for (size_t i = 0; i < key.length; i++) {\n"
            "        h ^= key.data[i];\n"
            "        h *= 1099511628211ULL;\n"
            "    }\n"
            "    return h;\n"
            "}\n\n"
            "static inline bool nyx_table_%zu_eq(%s a, %s b)\n"
            "{\n"
            "    return a.length == b.length &&\n"
            "           memcmp(a.data, b.data, a.length) == 0;\n"
            "}\n\n"
            "static inline nyx_table_state_%zu *nyx_table_%zu_state_at(nyx_table t, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_store *store;\n"
            "    if (t.store >= (uint32_t)NIPPY_STORE_COUNT) "
            "nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
            "    store = &nyx_stores[t.store];\n"
            "    if (store->generation != t.generation) "
            "nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
            "    if (t.offset > store->used ||\n"
            "        sizeof(nyx_table_state_%zu) > store->used - t.offset) "
            "nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
            "    return (nyx_table_state_%zu *)(void *)(store->data + "
            "t.offset);\n"
            "}\n\n"
            "static inline nyx_table_state_%zu *nyx_table_%zu_state(nyx_table t)\n"
            "{ return nyx_table_%zu_state_at(t, NULL, 0, 0, 0); }\n\n",
            i, i, key_name, i, key_name, key_name,
            i, i, MESSAGE_RUNTIME_STORE_RANGE, MESSAGE_RUNTIME_STALE_HANDLE,
            i, MESSAGE_RUNTIME_HANDLE_BOUNDS, i, i, i, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        /* The real finder needs the descriptor's store; use a helper with the
           descriptor so it can locate both the state and the arrays. */
        if (!buffer_appendf(source,
            "static inline size_t nyx_table_%zu_find_in(nyx_table t, "
            "nyx_table_state_%zu *s, %s key)\n"
            "{\n"
            "    nyx_store *store = &nyx_stores[t.store];\n"
            "    %s *keys = (%s *)(void *)(store->data + s->keys_offset);\n"
            "    uint8_t *states = (uint8_t *)(void *)(store->data + "
            "s->states_offset);\n"
            "    size_t start = (size_t)(nyx_table_%zu_hash(key) %% "
            "s->buckets);\n"
            "    for (size_t p = 0; p < s->buckets; p++) {\n"
            "        size_t j = (start + p) %% s->buckets;\n"
            "        if (states[j] == 0) return SIZE_MAX;\n"
            "        if (states[j] == 1 && nyx_table_%zu_eq(keys[j], key)) "
            "return j;\n"
            "    }\n"
            "    return SIZE_MAX;\n"
            "}\n\n",
            i, i, key_name, key_name, key_name, i, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
            "static inline bool nyx_table_%zu_get_at(nyx_table t, %s key, %s *out, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_table_state_%zu *s = nyx_table_%zu_state_at(t, file, line, column, pos);\n"
            "    nyx_store *store = &nyx_stores[t.store];\n"
            "    %s *values = (%s *)(void *)(store->data + s->values_offset);\n"
            "    size_t p = nyx_table_%zu_find_in(t, s, key);\n"
            "    if (p == SIZE_MAX) return false;\n"
            "    *out = values[p];\n"
            "    return true;\n"
            "}\n\n"
            "static inline bool nyx_table_%zu_get(nyx_table t, %s key, %s *out)\n"
            "{ return nyx_table_%zu_get_at(t, key, out, NULL, 0, 0, 0); }\n\n"
            "static inline bool nyx_table_%zu_has(nyx_table t, %s key, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_table_state_%zu *s = nyx_table_%zu_state_at(t, file, line, column, pos);\n"
            "    return nyx_table_%zu_find_in(t, s, key) != SIZE_MAX;\n"
            "}\n\n"
            "static inline int32_t nyx_table_%zu_len(nyx_table t, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    return (int32_t)nyx_table_%zu_state_at(t, file, line, column, pos)->length;\n"
            "}\n\n",
            i, key_name, value_name, i, i, value_name, value_name, i,
            i, key_name, value_name, i,
            i, key_name, i, i, i, i, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
            "static inline void nyx_table_%zu_grow(nyx_table *t, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_table_state_%zu *s = nyx_table_%zu_state_at(*t, file, line, column, pos);\n"
            "    nyx_store *store = &nyx_stores[t->store];\n"
            "    size_t old_buckets = s->buckets;\n"
            "    size_t old_length = s->length;\n"
            "    size_t old_keys = s->keys_offset;\n"
            "    size_t old_values = s->values_offset;\n"
            "    size_t old_states = s->states_offset;\n"
            "    size_t new_capacity = s->capacity == 0 ? 1 : s->capacity * 2;\n"
            "    size_t new_buckets = new_capacity * 2 + 1;\n"
            "    size_t new_keys = nyx_store_alloc_bytes_aligned_at(store, "
            "t->store, sizeof(%s) * new_buckets, _Alignof(%s), file, line, column, pos);\n"
            "    size_t new_values = nyx_store_alloc_bytes_aligned_at(store, "
            "t->store, sizeof(%s) * new_buckets, _Alignof(%s), file, line, column, pos);\n"
            "    size_t new_states = nyx_store_alloc_bytes_aligned_at(store, "
            "t->store, new_buckets, 1, file, line, column, pos);\n"
            "    %s *keys;\n"
            "    %s *values;\n"
            "    uint8_t *states;\n"
            "    memset(store->data + new_states, 0, new_buckets);\n"
            "    s = nyx_table_%zu_state_at(*t, file, line, column, pos);\n"
            "    keys = (%s *)(void *)(store->data + new_keys);\n"
            "    values = (%s *)(void *)(store->data + new_values);\n"
            "    states = (uint8_t *)(void *)(store->data + new_states);\n"
            "    for (size_t i = 0; i < old_buckets; i++) {\n"
            "        uint8_t *old_state = (uint8_t *)(void *)(store->data + "
            "old_states);\n"
            "        if (old_state[i] != 1) continue;\n"
            "        %s key = *(%s *)(void *)(store->data + old_keys + "
            "i * sizeof(%s));\n"
            "        %s value = *(%s *)(void *)(store->data + old_values + "
            "i * sizeof(%s));\n"
            "        size_t start = (size_t)(nyx_table_%zu_hash(key) %% "
            "new_buckets);\n"
            "        for (size_t p = 0; p < new_buckets; p++) {\n"
            "            size_t j = (start + p) %% new_buckets;\n"
            "            if (states[j] == 0) {\n"
            "                keys[j] = key; values[j] = value; states[j] = 1; "
            "break;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    s->buckets = new_buckets;\n"
            "    s->capacity = new_capacity;\n"
            "    s->length = old_length;\n"
            "    s->keys_offset = new_keys;\n"
            "    s->values_offset = new_values;\n"
            "    s->states_offset = new_states;\n"
            "}\n\n"
            "static inline void nyx_table_%zu_set(nyx_table *t, %s key, %s value, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_table_state_%zu *s = nyx_table_%zu_state_at(*t, file, line, column, pos);\n"
            "    size_t p = nyx_table_%zu_find_in(*t, s, key);\n"
            "    if (p != SIZE_MAX) {\n"
            "        nyx_store *store = &nyx_stores[t->store];\n"
            "        %s *values = (%s *)(void *)(store->data + "
            "s->values_offset);\n"
            "        values[p] = value;\n"
            "        return;\n"
            "    }\n"
            "    if (s->length >= s->capacity) { nyx_table_%zu_grow(t, file, line, column, pos); "
            "s = nyx_table_%zu_state_at(*t, file, line, column, pos); }\n"
            "    nyx_store *store = &nyx_stores[t->store];\n"
            "    %s *keys = (%s *)(void *)(store->data + s->keys_offset);\n"
            "    %s *values = (%s *)(void *)(store->data + s->values_offset);\n"
            "    uint8_t *states = (uint8_t *)(void *)(store->data + "
            "s->states_offset);\n"
            "    size_t start = (size_t)(nyx_table_%zu_hash(key) %% "
            "s->buckets);\n"
            "    for (size_t q = 0; q < s->buckets; q++) {\n"
            "        size_t j = (start + q) %% s->buckets;\n"
            "        if (states[j] != 1) {\n"
            "            keys[j] = key; values[j] = value; states[j] = 1;\n"
            "            s->length++;\n"
            "            return;\n"
            "        }\n"
            "    }\n"
            "    nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
            "}\n\n",
            i, i, i, key_name, key_name, value_name, value_name, key_name,
            value_name, i, key_name, value_name, key_name, key_name,
            key_name, value_name, value_name, value_name, i,
            i, key_name, value_name, i, i, i, value_name, value_name,
            i, i, key_name, key_name, value_name, value_name, i,
            MESSAGE_RUNTIME_RECORD_FULL)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
        if (!buffer_appendf(source,
            "static inline bool nyx_table_%zu_remove(nyx_table *t, %s key, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_table_state_%zu *s = nyx_table_%zu_state_at(*t, file, line, column, pos);\n"
            "    size_t p = nyx_table_%zu_find_in(*t, s, key);\n"
            "    nyx_store *store;\n"
            "    uint8_t *states;\n"
            "    if (p == SIZE_MAX) return false;\n"
            "    store = &nyx_stores[t->store];\n"
            "    states = (uint8_t *)(void *)(store->data + s->states_offset);\n"
            "    states[p] = 2;\n"
            "    s->length--;\n"
            "    return true;\n"
            "}\n\n"
            "static inline %s nyx_table_%zu_get_required(nyx_table t, %s key, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    %s out;\n"
            "    if (!nyx_table_%zu_get_at(t, key, &out, file, line, column, pos)) "
            "nyx_map_fail_at(\"%s\", file, line, column, pos);\n"
            "    return out;\n"
            "}\n\n"
            "static inline nyx_table nyx_table_%zu_create(nyx_store *store, "
            "uint32_t id, int32_t cap, const char *file, size_t line, size_t column, size_t pos)\n"
            "{\n"
            "    nyx_table_state_%zu *s;\n"
            "    nyx_table result;\n"
            "    size_t capacity = cap < 0 ? 0 : (size_t)cap;\n"
            "    size_t buckets = capacity == 0 ? 1 : capacity * 2 + 1;\n"
            "    size_t state_offset = nyx_store_alloc_bytes_aligned_at(store, "
            "id, sizeof(nyx_table_state_%zu), _Alignof(nyx_table_state_%zu), file, line, column, pos);\n"
            "    size_t keys_offset = nyx_store_alloc_bytes_aligned_at(store, "
            "id, sizeof(%s) * buckets, _Alignof(%s), file, line, column, pos);\n"
            "    size_t values_offset = nyx_store_alloc_bytes_aligned_at(store, "
            "id, sizeof(%s) * buckets, _Alignof(%s), file, line, column, pos);\n"
            "    size_t states_offset = nyx_store_alloc_bytes_aligned_at(store, "
            "id, buckets, 1, file, line, column, pos);\n"
            "    memset(store->data + states_offset, 0, buckets);\n"
            "    s = (nyx_table_state_%zu *)(void *)(store->data + "
            "state_offset);\n"
            "    s->buckets = buckets;\n"
            "    s->length = 0;\n"
            "    s->capacity = capacity;\n"
            "    s->keys_offset = keys_offset;\n"
            "    s->values_offset = values_offset;\n"
            "    s->states_offset = states_offset;\n"
            "    result.store = id;\n"
            "    result.offset = state_offset;\n"
            "    result.generation = store->generation;\n"
            "    return result;\n"
            "}\n\n",
            i, key_name, i, i, i, value_name, i, key_name, value_name, i,
            MESSAGE_RUNTIME_MAP_MISSING, i, i, i, i, key_name, key_name,
            value_name, value_name, i)) {
            error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
            return false;
        }
    }
    {
        size_t n;

        for (n = 0; n < program->nodes.length; n++) {
            const TypedNode *node = typed_program_node(program, n);
            const TypedNode *receiver;
            const TypedType *receiver_type;
            const TypedType *union_type;
            size_t key_type;
            size_t value_type;
            size_t nil_type = TYPED_NO_NODE;
            size_t nil_tag;
            char union_name[NIPPY_IDENTIFIER_MAX + 32];
            char key_name[64];
            char value_name[64];
            TextBuffer body;
            size_t j;

            if (node == NULL || node->kind != TYPED_TABLE_GET_OPTIONAL) {
                continue;
            }
            receiver = typed_program_node(program, node->left_child);
            receiver_type = receiver == NULL
                                ? NULL
                                : typed_program_type(program,
                                                     receiver->type_index);
            if (receiver_type == NULL ||
                receiver_type->kind != NIPPY_TYPE_TABLE ||
                receiver_type->child_count != 2) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            union_type = typed_program_type(program, node->type_index);
            key_type = *(const size_t *)list_get(&program->type_children,
                                                receiver_type->first_child);
            value_type = *(const size_t *)list_get(
                &program->type_children, receiver_type->first_child + 1);
            if (union_type == NULL ||
                !map_key_names(program, key_type, value_type, key_name,
                               sizeof(key_name), value_name,
                               sizeof(value_name), errors)) {
                error_report(errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION);
                return false;
            }
            buffer_init(&body);
            if (union_type->kind == NIPPY_TYPE_NIL) {
                if (!buffer_appendf(
                        &body,
                        "static NIPPY_UNUSED nyx_nil "
                        "nyx_table_%zu_get_optional_%zu(nyx_table t, %s key, const char *file, size_t line, size_t column, size_t pos)\n"
                        "{\n"
                        "    %s value;\n"
                        "    (void)nyx_table_%zu_get_at(t, key, &value, file, line, column, pos);\n"
                        "    return (nyx_nil)0;\n"
                        "}\n\n",
                        receiver->type_index, n, key_name, value_name,
                        receiver->type_index)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    buffer_free(&body);
                    return false;
                }
                if (!buffer_append(source, body.text)) {
                    error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                    buffer_free(&body);
                    return false;
                }
                buffer_free(&body);
                continue;
            }
            if (!format_c_type(program, node->type_index, union_name,
                               sizeof(union_name), errors)) {
                buffer_free(&body);
                return false;
            }
            for (j = 0; j < program->types.length; j++) {
                const TypedType *candidate = typed_program_type(program, j);

                if (candidate != NULL && candidate->kind == NIPPY_TYPE_NIL) {
                    nil_type = j;
                    break;
                }
            }
            if (nil_type == TYPED_NO_NODE ||
                !find_union_alternative(program, union_type, nil_type,
                                        &nil_tag) ||
                !buffer_appendf(
                    &body,
                    "static NIPPY_UNUSED %s nyx_table_%zu_get_optional_%zu("
                    "nyx_table t, %s key, const char *file, size_t line, size_t column, size_t pos)\n"
                    "{\n"
                    "    %s result;\n"
                    "    %s value;\n\n"
                    "    if (!nyx_table_%zu_get_at(t, key, &value, file, line, column, pos)) {\n"
                    "        result = (%s){ .tag = %zu };\n"
                    "        return result;\n"
                    "    }\n",
                    union_name, receiver->type_index, n, key_name, union_name,
                    value_name, receiver->type_index, union_name, nil_tag) ||
                !append_optional_union(&body, program, node->type_index,
                                       node->auxiliary_type, union_name,
                                       "value", "result") ||
                !buffer_append(&body, "    return result;\n}\n\n") ||
                !buffer_append(source, body.text)) {
                error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
                buffer_free(&body);
                return false;
            }
            buffer_free(&body);
        }
    }
    return true;
}
