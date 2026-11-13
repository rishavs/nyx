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

typedef int32_t (*nyx_fun_0)(nyx_sum_2);
typedef int32_t (*nyx_fun_1)(int32_t);

int32_t nyx_program_value(void);

#endif
