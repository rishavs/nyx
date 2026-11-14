#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIPPY_RECURSION_LIMIT 10000

#line 1 "test.nip"
static size_t nyx_depth_nyx_u_countdown = 0;

int32_t nyx_inner_nyx_u_countdown(int32_t nyx_u_n);

int32_t nyx_u_countdown(int32_t nyx_u_n)
{
    if (++nyx_depth_nyx_u_countdown > NIPPY_RECURSION_LIMIT) {
        fflush(stdout); fprintf(stderr, "nippy: %s:%zu:%zu (byte %zu): %s\n", __FILE__, (size_t)1, (size_t)1, (size_t)0, "nippy: recursion depth exceeded" + sizeof("nippy: ") - 1); exit(1);
    }
    int32_t nyx_recursion_result = nyx_inner_nyx_u_countdown(nyx_u_n);
    nyx_depth_nyx_u_countdown--;
    return nyx_recursion_result;
}

int32_t nyx_inner_nyx_u_countdown(int32_t nyx_u_n)
{
    (void)nyx_u_n;
#line 2 "test.nip"
    if ((nyx_u_n == ((int64_t)0ULL))) {
#line 3 "test.nip"
        return 0;
    }
#line 5 "test.nip"
    return nyx_u_countdown((int32_t)((uint32_t)(nyx_u_n) - (uint32_t)(1)));
}

int32_t nyx_program_value(void)
{
#line 8 "test.nip"
    int32_t nyx_u_zero = 0;
    return nyx_u_zero;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
