#include "test.h"

int main(void)
{
#line 3 "test.nip"
    nyx_sum_3 nyx_u_a = ((nyx_sum_3){ .tag = 0, .value.v0 = 3 });
    (void)nyx_u_a;
#line 4 "test.nip"
    nyx_sum_3 nyx_u_b = ((nyx_sum_3){ .tag = 1, .value.v1 = true });
    (void)nyx_u_b;
#line 5 "test.nip"
    nyx_sum_3 nyx_u_c = ((nyx_sum_3){ .tag = 2 });
    (void)nyx_u_c;
    return 0;
}
