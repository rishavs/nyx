#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void nyx_string_bounds(const char *file, size_t line, size_t column, size_t offset)
{
    fflush(stdout);
    fprintf(stderr, "nyx: %s:%zu:%zu (byte %zu): %s\n", file, line, column, offset, "nyx: string index out of bounds" + sizeof("nyx: ") - 1);
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

static inline void nyx_print_26(nyx_string_32 value, int depth, const char *file, size_t line, size_t column, size_t pos);
static inline void nyx_print_26(nyx_string_32 value, int depth, const char *file, size_t line, size_t column, size_t pos)
{
    (void)value; (void)file; (void)line; (void)column; (void)pos;
    if (depth > 16) {
        fputs("<...>", stdout);
        return;
    }
    if (value.length != 0) {
        fwrite(value.data, 1, value.length, stdout);
    }
}


int main(void)
{
#line 1 "test.nip"
    nyx_error nyx_u_e = (nyx_error){ ((nyx_sum_27){ .tag = 4, .value.v4 = (nyx_u_nyx_gen_12){ 0 } }), (nyx_string_128){ { 115, 108, 111, 119, 0 }, 4 }, (nyx_string_128){ { 116, 101, 115, 116, 46, 110, 105, 112, 0 }, 8 }, 1, 23, 22 };
    (void)nyx_u_e;
#line 2 "test.nip"
    {
        nyx_string_32 nyx_print_arg_59_0 = nyx_error_header(nyx_u_e);
        nyx_print_26(nyx_print_arg_59_0, 0, __FILE__, 2, 1, 59);
        fputc('\n', stdout);
    }
    return 0;
}
