#ifndef NIPPY_GENERATED_H
#define NIPPY_GENERATED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t data[9];
    uint32_t length;
} nyx_string_8;

static uint8_t nyx_string_8_at(nyx_string_8 s, int32_t i);

typedef uint8_t nyx_nil;

typedef struct {
    uint32_t tag;
    union {
        nyx_string_8 v1;
    } value;
} nyx_sum_2;

#endif
