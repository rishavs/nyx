#ifndef NIPPY_GENERATED_H
#define NIPPY_GENERATED_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t nyx_nil;

typedef struct {
    uint32_t tag;
    union {
        int32_t v0;
    } value;
} nyx_sum_2;

#line 1 "test.nip"
typedef struct {
    nyx_sum_2 nyx_u_value;
} nyx_u_nyx_gen_0;

typedef int32_t (*nyx_fun_0)(nyx_sum_2);

#endif
