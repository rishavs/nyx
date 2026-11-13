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
    nyx_sum_2 nyx_u_u = ((nyx_sum_2){ .tag = 1, .value.v1 = (nyx_string_8){ { 104, 105, 0 }, 2 } });
    (void)nyx_u_u;
#line 2 "test.nip"
    bool nyx_u_ok = (nyx_u_u.tag == 1);
    (void)nyx_u_ok;
    return 0;
}
