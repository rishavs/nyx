#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_nyx_genfn_1(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 2 "test.nip"
    return nyx_u_x;
}

#line 5 "test.nip"
int32_t nyx_u_nyx_genfn_0(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 6 "test.nip"
    return nyx_u_nyx_genfn_1(nyx_u_x);
}

int32_t nyx_program_value(void)
{
#line 9 "test.nip"
    int32_t nyx_u_n = nyx_u_nyx_genfn_0(3);
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
