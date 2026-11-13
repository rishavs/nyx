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

typedef int32_t (*nyx_fun_0)(nyx_handle);

int32_t nyx_program_value(void);

#endif
