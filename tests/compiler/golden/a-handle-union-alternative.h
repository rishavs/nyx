#ifndef NIPPY_GENERATED_H
#define NIPPY_GENERATED_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t store;
    size_t offset;
    uint64_t generation;
} nyx_handle;

typedef struct {
    unsigned char *data;
    size_t used;
    size_t capacity;
    uint64_t generation;
} nyx_store;

typedef uint8_t nyx_nil;

typedef struct {
    uint32_t tag;
    union {
        nyx_handle v1;
    } value;
} nyx_sum_3;

#endif
