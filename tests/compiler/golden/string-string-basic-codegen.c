#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void nyx_string_bounds(void)
{
    fprintf(stderr, "nippy: string index out of bounds\n");
    exit(1);
}

static uint8_t nyx_string_8_at(nyx_string_8 s, int32_t i)
{
    if (i < 0 || (uint32_t)i >= s.length) {
        nyx_string_bounds();
    }
    return s.data[i];
}

static bool nyx_string_8_equal(nyx_string_8 a, nyx_string_8 b)
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
