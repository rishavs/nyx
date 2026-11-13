#ifndef NIPPY_GENERATED_H
#define NIPPY_GENERATED_H

#include <stdbool.h>
#include <stdint.h>

#line 1 "test.nip"
typedef struct {
    int32_t nyx_u_x;
    int32_t nyx_u_y;
} nyx_u_nyx_gen_0;

typedef uint8_t nyx_nil;

typedef struct {
    uint32_t tag;
    union {
        nyx_u_nyx_gen_0 v1;
    } value;
} nyx_sum_3;

typedef nyx_u_nyx_gen_0 (*nyx_fun_0)(nyx_sum_3);

#endif
