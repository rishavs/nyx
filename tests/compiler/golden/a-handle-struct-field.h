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

#line 1 "test.nip"
typedef struct {
    nyx_handle nyx_u_h;
} nyx_u_nyx_gen_0;

#endif
