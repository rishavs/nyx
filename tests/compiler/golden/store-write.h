#ifndef NIPPY_GENERATED_H
#define NIPPY_GENERATED_H

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

#endif
