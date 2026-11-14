#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void nyx_string_bounds(const char *file, size_t line, size_t column, size_t offset)
{
    fflush(stdout);
    fprintf(stderr, "nippy: %s:%zu:%zu (byte %zu): %s\n", file, line, column, offset, "nippy: string index out of bounds" + sizeof("nippy: ") - 1);
    exit(1);
}

static inline uint8_t nyx_string_8_at(nyx_string_8 s, int32_t i, const char *file, size_t line, size_t column, size_t offset)
{
    if (i < 0 || (uint32_t)i >= s.length) {
        nyx_string_bounds(file, line, column, offset);
    }
    return s.data[i];
}

static inline bool nyx_string_8_equal(nyx_string_8 a, nyx_string_8 b)
{
    return a.length == b.length &&
           memcmp(a.data, b.data, a.length) == 0;
}

int main(void)
{
#line 1 "test.nip"
    nyx_string_8 nyx_u_s = (nyx_string_8){ { 104, 105, 10, 0 }, 3 };
    (void)nyx_u_s;
#line 2 "test.nip"
    int32_t nyx_u_n = ((int32_t)(nyx_u_s).length);
    (void)nyx_u_n;
#line 3 "test.nip"
    bool nyx_u_ok = (nyx_string_8_equal(nyx_u_s, (nyx_string_8){ { 104, 105, 10, 0 }, 3 }));
    (void)nyx_u_ok;
    return 0;
}
