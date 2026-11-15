#include "test.h"

#include <stdio.h>

#line 5 "test.nip"
int32_t nyx_u_inc(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 6 "test.nip"
    return (int32_t)((uint32_t)(nyx_u_x) + (uint32_t)(1));
}

#line 1 "test.nip"
int32_t nyx_u_nyx_genfn_0(nyx_fun_3 nyx_u_f, int32_t nyx_u_x)
{
    (void)nyx_u_f;
    (void)nyx_u_x;
#line 2 "test.nip"
    return nyx_u_f(nyx_u_x);
}

int32_t nyx_program_value(void)
{
    int32_t nyx_match_1;
    nyx_fun_0 nyx_match_0;
#line 9 "test.nip"
    int32_t nyx_u_n = (nyx_match_0 = nyx_u_inc, (nyx_match_1 = 1, nyx_u_nyx_genfn_0(nyx_match_0, nyx_match_1)));
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
