#include "test.h"

int main(void)
{
#line 4 "test.nip"
    nyx_sum_4 nyx_u_v = ((nyx_sum_4){ .tag = 1, .value.v1 = true });
    (void)nyx_u_v;
#line 5 "test.nip"
    nyx_sum_4 nyx_u_n = ((nyx_sum_4){ .tag = 0, .value.v0 = 3 });
    (void)nyx_u_n;
    return 0;
}
