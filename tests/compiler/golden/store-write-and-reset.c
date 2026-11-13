#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIPPY_STORE_COUNT 1

static nyx_store nyx_stores[NIPPY_STORE_COUNT];

static void nyx_panic(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static size_t nyx_store_align(size_t offset, size_t alignment)
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
        nyx_panic("nippy: out of memory");
    }
    return offset + (alignment - remainder);
}

static void nyx_store_init(nyx_store *store, size_t initial_bytes)
{
    if (initial_bytes == 0) {
        initial_bytes = 1;
    }
    store->data = malloc(initial_bytes);
    if (store->data == NULL) {
        nyx_panic("nippy: out of memory");
    }
    store->used = 0;
    store->capacity = initial_bytes;
    store->generation = 1;
}

static void nyx_store_reserve(nyx_store *store, size_t required)
{
    size_t capacity;
    unsigned char *grown;

    if (required <= store->capacity) {
        return;
    }
    capacity = store->capacity == 0 ? 1 : store->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) {
            nyx_panic("nippy: out of memory");
        }
        capacity *= 2;
    }
    grown = realloc(store->data, capacity);
    if (grown == NULL) {
        nyx_panic("nippy: out of memory");
    }
    store->data = grown;
    store->capacity = capacity;
}

static nyx_handle nyx_store_alloc(nyx_store *store, size_t id,
                                      const void *value, size_t size,
                                      size_t align)
{
    nyx_handle handle;
    size_t offset;
    size_t end;

    if (id >= (size_t)NIPPY_STORE_COUNT) {
        nyx_panic("nippy: invalid store id");
    }
    if (size == 0) {
        nyx_panic("nippy: handle is out of range");
    }
    offset = nyx_store_align(store->used, align);
    if (offset > (size_t)-1 - size) {
        nyx_panic("nippy: handle is out of range");
    }
    end = offset + size;
    nyx_store_reserve(store, end);
    memcpy(store->data + offset, value, size);
    store->used = end;
    handle.store = (uint32_t)id;
    handle.offset = offset;
    handle.generation = store->generation;
    return handle;
}

static unsigned char *nyx_handle_pointer(nyx_handle handle,
                                           size_t size)
{
    nyx_store *store;

    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {
        nyx_panic("nippy: invalid store id");
    }
    store = &nyx_stores[handle.store];
    if (handle.generation != store->generation) {
        nyx_panic("nippy: stale handle");
    }
    if (handle.offset > store->used ||
        size > store->used - handle.offset) {
        nyx_panic("nippy: handle is out of range");
    }
    return store->data + handle.offset;
}

static void nyx_handle_write(nyx_handle handle, const void *value,
                               size_t size)
{
    memcpy(nyx_handle_pointer(handle, size), value, size);
}

static void nyx_store_reset(nyx_store *store)
{
    store->used = 0;
    if (store->generation == UINT64_MAX) {
        nyx_panic("nippy: stale handle");
    }
    store->generation++;
}

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
    nyx_store_init(&nyx_stores[0], 16);
#line 2 "test.nip"
    nyx_handle nyx_u_p = nyx_store_alloc(&nyx_stores[0], 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));
    (void)nyx_u_p;
#line 3 "test.nip"
    nyx_nil nyx_u_w = ((void)nyx_handle_write(nyx_u_p, &(int32_t){ 2 }, sizeof(int32_t)), (nyx_nil)0);
    (void)nyx_u_w;
#line 4 "test.nip"
    nyx_nil nyx_u_r = ((void)nyx_store_reset(&nyx_stores[0]), (nyx_nil)0);
    (void)nyx_u_r;
    nyx_store_free(&nyx_stores[0]);
    return 0;
}
