#ifndef NIPPY_GENERATED_H
#define NIPPY_GENERATED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t data[33];
    uint32_t length;
} nyx_string_32;

static uint8_t nyx_string_32_at(nyx_string_32 s, int32_t i);

typedef struct {
    uint8_t data[129];
    uint32_t length;
} nyx_string_128;

static uint8_t nyx_string_128_at(nyx_string_128 s, int32_t i);

typedef struct {
    nyx_string_32 header;
    nyx_string_128 description;
    nyx_string_128 filename;
    int32_t line;
    int32_t col;
    int32_t pos;
} nyx_error;

#endif
