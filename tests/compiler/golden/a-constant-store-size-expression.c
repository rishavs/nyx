#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIPPY_STORE_COUNT 1

static nyx_store nyx_stores[NIPPY_STORE_COUNT];

static void nyx_panic(const char *message)
{
    fflush(stdout);
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static inline void nyx_panic_at(const char *message, const char *file, size_t line, size_t column, size_t pos)
{
    if (file == NULL) nyx_panic(message);
    fflush(stdout);
    fprintf(stderr, "nippy: %s:%zu:%zu (byte %zu): %s\n", file, line, column, pos, message + 7);
    exit(1);
}

static inline size_t nyx_store_align_at(size_t offset, size_t alignment, const char *file, size_t line, size_t column, size_t pos)
{
    size_t remainder;

    if (alignment <= 1) {
        return offset;
    }
    remainder = offset % alignment;
    if (remainder == 0) {
        return offset;
    }
    if (offset > (size_t)-1 - (alignment - remainder)) {
        nyx_panic_at("nippy: out of memory", file, line, column, pos);
    }
    return offset + (alignment - remainder);
}

static inline size_t nyx_store_align(size_t offset, size_t alignment)
{ return nyx_store_align_at(offset, alignment, NULL, 0, 0, 0); }

static inline void nyx_store_init_at(nyx_store *store, size_t initial_bytes, const char *file, size_t line, size_t column, size_t pos)
{
    if (initial_bytes == 0) {
        initial_bytes = 1;
    }
    store->data = malloc(initial_bytes);
    if (store->data == NULL) {
        nyx_panic_at("nippy: out of memory", file, line, column, pos);
    }
    store->used = 0;
    store->capacity = initial_bytes;
    store->generation = 1;
}

static inline void nyx_store_init(nyx_store *store, size_t size)
{ nyx_store_init_at(store, size, NULL, 0, 0, 0); }

static inline void nyx_store_reserve_at(nyx_store *store, size_t required, const char *file, size_t line, size_t column, size_t pos)
{
    size_t capacity;
    unsigned char *grown;

    if (required <= store->capacity) {
        return;
    }
    capacity = store->capacity == 0 ? 1 : store->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) {
            nyx_panic_at("nippy: out of memory", file, line, column, pos);
        }
        capacity *= 2;
    }
    grown = realloc(store->data, capacity);
    if (grown == NULL) {
        nyx_panic_at("nippy: out of memory", file, line, column, pos);
    }
    store->data = grown;
    store->capacity = capacity;
}

static inline void nyx_store_reserve(nyx_store *store, size_t required)
{ nyx_store_reserve_at(store, required, NULL, 0, 0, 0); }

static inline nyx_handle nyx_store_alloc_at(nyx_store *store, size_t id,
                                      const void *value, size_t size,
                                      size_t align, const char *file, size_t line, size_t column, size_t pos)
{
    nyx_handle handle;
    size_t offset;
    size_t end;

    if (id >= (size_t)NIPPY_STORE_COUNT) {
        nyx_panic_at("nippy: invalid store id", file, line, column, pos);
    }
    if (size == 0) {
        nyx_panic_at("nippy: handle is out of range", file, line, column, pos);
    }
    offset = nyx_store_align_at(store->used, align, file, line, column, pos);
    if (offset > (size_t)-1 - size) {
        nyx_panic_at("nippy: handle is out of range", file, line, column, pos);
    }
    end = offset + size;
    nyx_store_reserve_at(store, end, file, line, column, pos);
    memcpy(store->data + offset, value, size);
    store->used = end;
    handle.store = (uint32_t)id;
    handle.offset = offset;
    handle.generation = store->generation;
    return handle;
}

static inline nyx_handle nyx_store_alloc(nyx_store *store, size_t id, const void *value, size_t size, size_t align)
{ return nyx_store_alloc_at(store, id, value, size, align, NULL, 0, 0, 0); }

static inline unsigned char *nyx_handle_pointer_at(nyx_handle handle,
                                           size_t size, const char *file, size_t line, size_t column, size_t pos)
{
    nyx_store *store;

    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {
        nyx_panic_at("nippy: invalid store id", file, line, column, pos);
    }
    store = &nyx_stores[handle.store];
    if (handle.generation != store->generation) {
        nyx_panic_at("nippy: stale handle", file, line, column, pos);
    }
    if (handle.offset > store->used ||
        size > store->used - handle.offset) {
        nyx_panic_at("nippy: handle is out of range", file, line, column, pos);
    }
    return store->data + handle.offset;
}

static inline unsigned char *nyx_handle_pointer(nyx_handle handle, size_t size)
{ return nyx_handle_pointer_at(handle, size, NULL, 0, 0, 0); }

static inline void nyx_handle_write_at(nyx_handle handle, const void *value,
                               size_t size, const char *file, size_t line, size_t column, size_t pos)
{
    memcpy(nyx_handle_pointer_at(handle, size, file, line, column, pos), value, size);
}

static inline void nyx_handle_write(nyx_handle handle, const void *value, size_t size)
{ nyx_handle_write_at(handle, value, size, NULL, 0, 0, 0); }

static inline void nyx_store_reset_at(nyx_store *store, const char *file, size_t line, size_t column, size_t pos)
{
    store->used = 0;
    if (store->generation == UINT64_MAX) {
        nyx_panic_at("nippy: stale handle", file, line, column, pos);
    }
    store->generation++;
}

static inline void nyx_store_reset(nyx_store *store)
{ nyx_store_reset_at(store, NULL, 0, 0, 0); }

static void nyx_store_free(nyx_store *store)
{
    free(store->data);
    store->data = NULL;
    store->used = 0;
    store->capacity = 0;
    store->generation = 0;
    (void)nyx_store_reserve;
    (void)nyx_store_alloc;
    (void)nyx_handle_pointer;
    (void)nyx_handle_write;
    (void)nyx_store_reset;
}

int main(void)
{
#line 1 "test.nip"
    nyx_store_init_at(&nyx_stores[0], 3, __FILE__, 1, 1, 0);
#line 2 "test.nip"
    nyx_handle nyx_u_h = nyx_store_alloc_at(&nyx_stores[0], 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t), __FILE__, 2, 26, 53);
    (void)nyx_u_h;
    nyx_store_free(&nyx_stores[0]);
    return 0;
}
