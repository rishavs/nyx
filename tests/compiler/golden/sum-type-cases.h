#ifndef NYX_GENERATED_H
#define NYX_GENERATED_H

#include <stdbool.h>
#include <stdint.h>

#line 2 "test.nip"
typedef struct {
    int32_t nyx_u_radius;
} nyx_u_nyx_gen_0;

#line 3 "test.nip"
typedef struct {
    int32_t nyx_u_side;
} nyx_u_nyx_gen_1;

#line 4 "test.nip"
typedef struct {
    char nyx_unit;
} nyx_u_nyx_gen_2;

typedef struct {
    uint32_t tag;
    union {
        nyx_u_nyx_gen_0 v0;
        nyx_u_nyx_gen_1 v1;
        nyx_u_nyx_gen_2 v2;
    } value;
} nyx_sum_4;

typedef uint8_t nyx_nil;

#endif
