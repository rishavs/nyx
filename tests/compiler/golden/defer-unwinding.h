#ifndef NYX_GENERATED_H
#define NYX_GENERATED_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct nyx_store nyx_store;

typedef struct {
    nyx_store *store;
    size_t offset;
    uint64_t generation;
} nyx_handle;

struct nyx_store {
    unsigned char *data;
    size_t used;
    size_t capacity;
    uint64_t generation;
};

typedef uint8_t nyx_nil;

typedef struct {
    const uint8_t *static_data;
    nyx_store *store;
    size_t offset;
    size_t length;
    uint64_t generation;
} nyx_string;

typedef struct { int32_t start, end; int64_t step; } nyx_range_6;

typedef int32_t (*nyx_fun_0)(int32_t);

#endif
