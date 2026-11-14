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

typedef struct {
    uint8_t data[9];
    uint32_t length;
} nyx_string_8;

static inline uint8_t nyx_string_8_at(nyx_string_8 s, int32_t i, const char *file, size_t line, size_t column, size_t offset);

#endif
