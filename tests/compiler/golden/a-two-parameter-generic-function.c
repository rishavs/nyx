#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_nyx_genfn_0(int32_t nyx_u_a, bool nyx_u_b)
{
    (void)nyx_u_a;
    (void)nyx_u_b;
#line 2 "test.nip"
    return nyx_u_a;
}

int32_t nyx_program_value(void)
{
#line 5 "test.nip"
    int32_t nyx_u_n = nyx_u_nyx_genfn_0(1, true);
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
