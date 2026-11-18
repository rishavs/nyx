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

typedef nyx_handle (*nyx_fun_0)(int32_t);
typedef int32_t (*nyx_fun_1)(nyx_handle);

int32_t nyx_program_value(void);

#endif
