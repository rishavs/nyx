#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void nyx_string_bounds(const char *file, size_t line, size_t column, size_t offset)
{
    fflush(stdout);
    fprintf(stderr, "nippy: %s:%zu:%zu (byte %zu): %s\n", file, line, column, offset, "nippy: string index out of bounds" + 7);
    exit(1);
}

static inline uint8_t nyx_string_32_at(nyx_string_32 s, int32_t i, const char *file, size_t line, size_t column, size_t offset)
{
    if (i < 0 || (uint32_t)i >= s.length) {
        nyx_string_bounds(file, line, column, offset);
    }
    return s.data[i];
}

static inline bool nyx_string_32_equal(nyx_string_32 a, nyx_string_32 b)
{
    return a.length == b.length &&
           memcmp(a.data, b.data, a.length) == 0;
}

static inline uint8_t nyx_string_128_at(nyx_string_128 s, int32_t i, const char *file, size_t line, size_t column, size_t offset)
{
    if (i < 0 || (uint32_t)i >= s.length) {
        nyx_string_bounds(file, line, column, offset);
    }
    return s.data[i];
}

static inline bool nyx_string_128_equal(nyx_string_128 a, nyx_string_128 b)
{
    return a.length == b.length &&
           memcmp(a.data, b.data, a.length) == 0;
}

int main(void)
{
#line 1 "test.nip"
    nyx_error nyx_u_e = (nyx_error){ (nyx_string_32){ { 69, 0 }, 1 }, (nyx_string_128){ { 100, 101, 115, 99, 0 }, 4 }, (nyx_string_128){ { 116, 101, 115, 116, 46, 110, 105, 112, 0 }, 8 }, 1, 2, 3 };
    (void)nyx_u_e;
#line 2 "test.nip"
    int32_t nyx_u_l = (nyx_u_e).line;
    (void)nyx_u_l;
#line 3 "test.nip"
    nyx_string_32 nyx_u_h = (nyx_u_e).header;
    (void)nyx_u_h;
    return 0;
}
