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

typedef bool (*nyx_fun_0)(nyx_sum_2);

#endif
