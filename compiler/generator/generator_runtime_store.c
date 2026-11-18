#include "generator_internal.h"

/* Appends `text` when `condition` holds. */
static void append_if(TextBuffer *source, bool condition, const char *text)
{
    if (condition) {
        buffer_append(source, text);
    }
}

static void emit_alloc_bytes(TextBuffer *source, bool pool)
{
        buffer_append(
                       source,
                       "static inline size_t nyx_store_alloc_bytes_at("
                       "nyx_store *store, size_t size, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n\n"
                       "    if (store == NULL) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    if (size == 0) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       );
        append_if(source, pool,
                       "    if (store->pool) {\n"
                       "        return nyx_pool_take(store, size, 1, file, line, column, pos);\n"
                       "    }\n");
        buffer_append(
                       source,
                       "    offset = store->used;\n"
                       "    if (offset > (size_t)-1 - size) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    nyx_store_reserve_at(store, offset + size, file, line, column, pos);\n"
                       "    store->used = offset + size;\n"
                       "    return offset;\n"
                       "}\n\n"
                       "static inline size_t nyx_store_alloc_bytes(nyx_store *store, size_t size)\n"
                       "{ return nyx_store_alloc_bytes_at(store, size, NULL, 0, 0, 0); }\n\n");
}

static void emit_alloc_bytes_aligned(TextBuffer *source, bool pool)
{
        buffer_append(
                       source,
                       "static inline size_t nyx_store_alloc_bytes_aligned_at("
                       "nyx_store *store, size_t size,\n"
                       "                                              "
                       "size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n\n"
                       "    if (store == NULL) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    if (size == 0) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       );
        append_if(source, pool,
                       "    if (store->pool) {\n"
                       "        return nyx_pool_take(store, size, alignment, file, line, column, pos);\n"
                       "    }\n");
        buffer_append(
                       source,
                       "    offset = nyx_store_align_at(store->used, alignment, file, line, column, pos);\n"
                       "    if (offset > (size_t)-1 - size) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    nyx_store_reserve_at(store, offset + size, file, line, column, pos);\n"
                       "    store->used = offset + size;\n"
                       "    return offset;\n"
                       "}\n\n"
                       "static inline size_t nyx_store_alloc_bytes_aligned(nyx_store *store, size_t size, size_t align)\n"
                       "{ return nyx_store_alloc_bytes_aligned_at(store, size, align, NULL, 0, 0, 0); }\n\n");
}

bool emit_store_runtime(GeneratorContext *gen, TextBuffer *source, size_t store_count,
                               bool needs_bytes_alloc,
                               bool needs_aligned_alloc,
                               bool pool, ErrorState *errors)
{
    (void)errors;
    if (store_count > 0) {
        buffer_appendf(
                       source,
                       "#define NYX_STORE_COUNT %zu\n\n"
                       "static nyx_store nyx_stores[NYX_STORE_COUNT];\n\n",
                       store_count);
    }
    buffer_append(
                   source,
                   "static void nyx_panic(const char *message)\n"
                   "{\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"%s\\n\", message);\n"
                   "    exit(1);\n"
                   "}\n\n"
                   "static inline void nyx_panic_at(const char *message, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (file == NULL) nyx_panic(message);\n"
                   "    fflush(stdout);\n"
                   "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos, message + sizeof(\"" MESSAGE_RUNTIME_PREFIX "\") - 1);\n"
                   "    exit(1);\n"
                   "}\n\n");
    buffer_append(
                   source,
                   "static inline size_t nyx_store_align_at(size_t offset, "
                   "size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    size_t remainder;\n\n"
                   "    if (alignment <= 1) {\n"
                   "        return offset;\n"
                   "    }\n"
                   "    remainder = offset % alignment;\n"
                   "    if (remainder == 0) {\n"
                   "        return offset;\n"
                   "    }\n"
                   "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return offset + (alignment - remainder);\n"
                   "}\n\n"
                   "static inline size_t nyx_store_align(size_t offset, size_t alignment)\n"
                   "{ return nyx_store_align_at(offset, alignment, NULL, 0, 0, 0); }\n\n");
    /* Threads allocate from a parallel block's store by moving one atomic
       cursor forward, so no two get the same bytes and none waits for another. */
    append_if(source, pool,
                       "static inline size_t nyx_pool_take(nyx_store *store, "
                       "size_t size, size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t used = atomic_load_explicit(&store->cursor, memory_order_seq_cst);\n\n"
                       "    for (;;) {\n"
                       "        size_t offset = nyx_store_align_at(used, alignment, file, line, column, pos);\n\n"
                       "        if (size > store->capacity || offset > store->capacity - size) {\n"
                       "            nyx_panic_at(\"" MESSAGE_RUNTIME_POOL_FULL "\", file, line, column, pos);\n"
                       "        }\n"
                       "        if (atomic_compare_exchange_weak_explicit(&store->cursor, &used, offset + size,\n"
                       "                                                   memory_order_seq_cst, memory_order_seq_cst)) {\n"
                       "            return offset;\n"
                       "        }\n"
                       "    }\n"
                       "}\n\n");
    buffer_append(
                   source,
                   "static inline void nyx_store_init_at(nyx_store *store, "
                   "size_t initial_bytes, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    if (initial_bytes == 0) {\n"
                   "        initial_bytes = 1;\n"
                   "    }\n"
                   "    store->data = malloc(initial_bytes);\n"
                   "    if (store->data == NULL) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    store->used = 0;\n"
                   "    store->capacity = initial_bytes;\n"
                   "    store->generation = 1;\n");
    append_if(source, pool,
                   "    store->pool = 0;\n"
                   "    atomic_init(&store->cursor, 0);\n");
    buffer_append(source,
                   "}\n\n"
                   "static inline void nyx_store_init(nyx_store *store, size_t size)\n"
                   "{ nyx_store_init_at(store, size, NULL, 0, 0, 0); }\n\n");
    buffer_append(
                   source,
                   "static inline void nyx_store_reserve_at(nyx_store *store, "
                   "size_t required, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    size_t capacity;\n"
                   "    unsigned char *grown;\n\n"
                   "    if (required <= store->capacity) {\n"
                   "        return;\n"
                   "    }\n"
                   "    capacity = store->capacity == 0 ? 1 : "
                   "store->capacity;\n"
                   "    while (capacity < required) {\n"
                   "        if (capacity > (size_t)-1 / 2) {\n"
                   "            nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "        }\n"
                   "        capacity *= 2;\n"
                   "    }\n"
                   "    grown = realloc(store->data, capacity);\n"
                   "    if (grown == NULL) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    store->data = grown;\n"
                   "    store->capacity = capacity;\n"
                   "}\n\n"
                   "static inline void nyx_store_reserve(nyx_store *store, size_t required)\n"
                   "{ nyx_store_reserve_at(store, required, NULL, 0, 0, 0); }\n\n");
    buffer_append(
                   source,
                   "static inline nyx_handle nyx_store_alloc_at(nyx_store *store, "
                   "const void *value, "
                   "size_t size,\n"
                   "                                      size_t align, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_handle handle;\n"
                   "    size_t offset;\n"
                   "    size_t end;\n\n"
                   "    if (store == NULL) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    if (size == 0) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   );
    append_if(source, pool,
                   "    if (store->pool) {\n"
                   "        offset = nyx_pool_take(store, size, align, file, line, column, pos);\n"
                   "        memcpy(store->data + offset, value, size);\n"
                   "        handle.store = store;\n"
                   "        handle.offset = offset;\n"
                   "        handle.generation = store->generation;\n"
                   "        return handle;\n"
                   "    }\n");
    buffer_append(
                   source,
                   "    offset = nyx_store_align_at(store->used, align, file, line, column, pos);\n"
                   "    if (offset > (size_t)-1 - size) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    end = offset + size;\n"
                   "    nyx_store_reserve_at(store, end, file, line, column, pos);\n"
                   "    memcpy(store->data + offset, value, size);\n"
                   "    store->used = end;\n"
                   "    handle.store = store;\n"
                   "    handle.offset = offset;\n"
                   "    handle.generation = store->generation;\n"
                   "    return handle;\n"
                   "}\n\n"
                   "static inline nyx_handle nyx_store_alloc(nyx_store *store, const void *value, size_t size, size_t align)\n"
                   "{ return nyx_store_alloc_at(store, value, size, align, NULL, 0, 0, 0); }\n\n");
    buffer_append(
                   source,
                   "static inline unsigned char *nyx_handle_pointer_at("
                   "nyx_handle handle,\n"
                   "                                           size_t size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store;\n\n"
                   "    if (handle.store == NULL) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    store = handle.store;\n"
                   );
    append_generation_check(source, gen, "store->generation",
                                 "handle.generation", "nyx_panic_at");
    buffer_append(
                   source,
                   "    if (handle.offset > store->used ||\n"
                   "        size > store->used - handle.offset) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return store->data + handle.offset;\n"
                   "}\n\n"
                   "static inline unsigned char *nyx_handle_pointer(nyx_handle handle, size_t size)\n"
                   "{ return nyx_handle_pointer_at(handle, size, NULL, 0, 0, 0); }\n\n");
    buffer_append(
                   source,
                   "static inline void nyx_handle_write_at(nyx_handle handle, "
                   "const void *value,\n"
                   "                               size_t size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    memcpy(nyx_handle_pointer_at(handle, size, file, line, column, pos), value, "
                   "size);\n"
                   "}\n\n"
                   "static inline void nyx_handle_write(nyx_handle handle, const void *value, size_t size)\n"
                   "{ nyx_handle_write_at(handle, value, size, NULL, 0, 0, 0); }\n\n");
    if (needs_bytes_alloc) {
        emit_alloc_bytes(source, pool);
    }
    if (needs_aligned_alloc) {
        emit_alloc_bytes_aligned(source, pool);
    }
    /* Referencing the helpers here keeps the whole runtime "used" even when a
       particular program never allocates or writes. Generated C is
       compiled with -Wall -Wextra. */
    buffer_append(
                   source,
                   "static NYX_UNUSED void nyx_store_free(nyx_store *store)\n"
                   "{\n"
                   "    free(store->data);\n"
                   "    store->data = NULL;\n"
                   "    store->used = 0;\n"
                   "    store->capacity = 0;\n"
                   "    store->generation = 0;\n"
                   "    (void)nyx_store_reserve;\n"
                   "    (void)nyx_store_alloc;\n"
                   "    (void)nyx_handle_pointer;\n"
                   "    (void)nyx_handle_write;\n"
                   "}\n\n");
    return true;
}


/* The C initialiser of an Atomic cell. A negative integer is stored as its
   magnitude minus one, so the most negative value of a type needs no literal
   outside the type's range. */
static void emit_atomic_cell(TextBuffer *source, const TypedNode *node,
                             const char *type)
{
    if (strcmp(type, "bool") == 0) {
        buffer_appendf(source, "static _Atomic(bool) nyx_atomic_%zu = %s;\n",
                       node->store_id,
                       node->integer_value_u != 0 ? "true" : "false");
    } else if (node->integer_value != 0) {
        buffer_appendf(
            source,
            "static _Atomic(%s) nyx_atomic_%zu = (%s)(-(long long)%lluLL - 1LL);\n",
            type, node->store_id, type,
            (unsigned long long)(node->integer_value_u - 1));
    } else {
        buffer_appendf(source, "static _Atomic(%s) nyx_atomic_%zu = (%s)%lluULL;\n",
                       type, node->store_id, type,
                       (unsigned long long)node->integer_value_u);
    }
}

/* `global x: Atomic<T> = Atomic(c)` becomes a static `_Atomic` cell, constant
   initialised, so no thread can see it before it has its value. */
bool emit_atomic_declarations(const TypedProgram *program, TextBuffer *source,
                              ErrorState *errors)
{
    size_t i;

    for (i = 0; i < program->roots.length; i++) {
        const TypedNode *node = typed_program_node(
            program, *(const size_t *)list_get(&program->roots, i));
        char type[NYX_IDENTIFIER_MAX + 32];

        if (node->kind != TYPED_ATOMIC_DECLARATION) {
            continue;
        }
        if (!format_c_type(program, node->type_index, type, sizeof(type),
                           errors)) {
            return false;
        }
        emit_atomic_cell(source, node, type);
    }
    buffer_append(source, "\n");
    return true;
}
