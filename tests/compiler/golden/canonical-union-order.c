#include "test.h"

int main(void)
{
#line 1 "test.nip"
    nyx_sum_2 nyx_u_a = ((nyx_sum_2){ .tag = 0, .value.v0 = 3 });
    (void)nyx_u_a;
#line 2 "test.nip"
    nyx_sum_2 nyx_u_b = ((nyx_sum_2){ .tag = 1, .value.v1 = false });
    (void)nyx_u_b;
    return 0;
}
