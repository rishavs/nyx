#include "generator_internal.h"

bool emit_store_runtime(TextBuffer *source, size_t store_count,
                               bool needs_bytes_alloc,
                               bool needs_aligned_alloc,
                               ErrorState *errors)
{
    bool ok = true;

    ok = ok && buffer_appendf(
                   source,
                   "#define NIPPY_STORE_COUNT %zu\n\n"
                   "static nyx_store nyx_stores[NIPPY_STORE_COUNT];\n\n",
                   store_count);
    ok = ok && buffer_append(
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
                   "    fprintf(stderr, \"" MESSAGE_RUNTIME_POSITION "\\n\", file, line, column, pos, message + 7);\n"
                   "    exit(1);\n"
                   "}\n\n");
    ok = ok && buffer_append(
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
    ok = ok && buffer_append(
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
                   "    store->generation = 1;\n"
                   "}\n\n"
                   "static inline void nyx_store_init(nyx_store *store, size_t size)\n"
                   "{ nyx_store_init_at(store, size, NULL, 0, 0, 0); }\n\n");
    ok = ok && buffer_append(
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
    ok = ok && buffer_append(
                   source,
                   "static inline nyx_handle nyx_store_alloc_at(nyx_store *store, "
                   "size_t id,\n"
                   "                                      const void *value, "
                   "size_t size,\n"
                   "                                      size_t align, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_handle handle;\n"
                   "    size_t offset;\n"
                   "    size_t end;\n\n"
                   "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    if (size == 0) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    offset = nyx_store_align_at(store->used, align, file, line, column, pos);\n"
                   "    if (offset > (size_t)-1 - size) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    end = offset + size;\n"
                   "    nyx_store_reserve_at(store, end, file, line, column, pos);\n"
                   "    memcpy(store->data + offset, value, size);\n"
                   "    store->used = end;\n"
                   "    handle.store = (uint32_t)id;\n"
                   "    handle.offset = offset;\n"
                   "    handle.generation = store->generation;\n"
                   "    return handle;\n"
                   "}\n\n"
                   "static inline nyx_handle nyx_store_alloc(nyx_store *store, size_t id, const void *value, size_t size, size_t align)\n"
                   "{ return nyx_store_alloc_at(store, id, value, size, align, NULL, 0, 0, 0); }\n\n");
    ok = ok && buffer_append(
                   source,
                   "static inline unsigned char *nyx_handle_pointer_at("
                   "nyx_handle handle,\n"
                   "                                           size_t size, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    nyx_store *store;\n\n"
                   "    if ((size_t)handle.store >= "
                   "(size_t)NIPPY_STORE_COUNT) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    store = &nyx_stores[handle.store];\n"
                   "    if (handle.generation != store->generation) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_STALE_HANDLE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    if (handle.offset > store->used ||\n"
                   "        size > store->used - handle.offset) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    return store->data + handle.offset;\n"
                   "}\n\n"
                   "static inline unsigned char *nyx_handle_pointer(nyx_handle handle, size_t size)\n"
                   "{ return nyx_handle_pointer_at(handle, size, NULL, 0, 0, 0); }\n\n");
    ok = ok && buffer_append(
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
        ok = ok && buffer_append(
                       source,
                       "static inline size_t nyx_store_alloc_bytes_at("
                       "nyx_store *store, uint32_t id, size_t size, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n\n"
                       "    if (id >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    if (size == 0) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    offset = store->used;\n"
                       "    if (offset > (size_t)-1 - size) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    nyx_store_reserve_at(store, offset + size, file, line, column, pos);\n"
                       "    store->used = offset + size;\n"
                       "    return offset;\n"
                       "}\n\n"
                       "static inline size_t nyx_store_alloc_bytes(nyx_store *store, uint32_t id, size_t size)\n"
                       "{ return nyx_store_alloc_bytes_at(store, id, size, NULL, 0, 0, 0); }\n\n");
    }
    if (needs_aligned_alloc) {
        ok = ok && buffer_append(
                       source,
                       "static inline size_t nyx_store_alloc_bytes_aligned_at("
                       "nyx_store *store, uint32_t id, size_t size,\n"
                       "                                              "
                       "size_t alignment, const char *file, size_t line, size_t column, size_t pos)\n"
                       "{\n"
                       "    size_t offset;\n\n"
                       "    if (id >= (uint32_t)NIPPY_STORE_COUNT) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_STORE_RANGE
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    if (size == 0) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_HANDLE_BOUNDS
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    offset = nyx_store_align_at(store->used, alignment, file, line, column, pos);\n"
                       "    if (offset > (size_t)-1 - size) {\n"
                       "        nyx_panic_at(\"" MESSAGE_RUNTIME_OUT_OF_MEMORY
                       "\", file, line, column, pos);\n"
                       "    }\n"
                       "    nyx_store_reserve_at(store, offset + size, file, line, column, pos);\n"
                       "    store->used = offset + size;\n"
                       "    return offset;\n"
                       "}\n\n"
                       "static inline size_t nyx_store_alloc_bytes_aligned(nyx_store *store, uint32_t id, size_t size, size_t align)\n"
                       "{ return nyx_store_alloc_bytes_aligned_at(store, id, size, align, NULL, 0, 0, 0); }\n\n");
    }
    ok = ok && buffer_append(
                   source,
                   "static inline void nyx_store_reset_at(nyx_store *store, const char *file, size_t line, size_t column, size_t pos)\n"
                   "{\n"
                   "    store->used = 0;\n"
                   "    if (store->generation == UINT64_MAX) {\n"
                   "        nyx_panic_at(\"" MESSAGE_RUNTIME_STALE_HANDLE
                   "\", file, line, column, pos);\n"
                   "    }\n"
                   "    store->generation++;\n"
                   "}\n\n"
                   "static inline void nyx_store_reset(nyx_store *store)\n"
                   "{ nyx_store_reset_at(store, NULL, 0, 0, 0); }\n\n");
    /* Referencing the helpers here keeps the whole runtime "used" even when a
       particular program never allocates, writes, or resets. Generated C is
       compiled with -Wall -Wextra. */
    ok = ok && buffer_append(
                   source,
                   "static void nyx_store_free(nyx_store *store)\n"
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
                   "    (void)nyx_store_reset;\n"
                   "}\n\n");
    if (!ok) {
        error_report(errors, MESSAGE_GENERATOR_OUT_OF_MEMORY);
        return false;
    }
    return true;
}
