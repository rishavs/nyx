#include "test.h"

#line 1 "test.nip"
nyx_sum_2 nyx_u_nyx_genfn_0(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 2 "test.nip"
    return ((nyx_sum_2){ .tag = 0, .value.v0 = nyx_u_x });
}

int main(void)
{
#line 5 "test.nip"
    nyx_sum_2 nyx_u_m = nyx_u_nyx_genfn_0(3);
    (void)nyx_u_m;
    return 0;
}
