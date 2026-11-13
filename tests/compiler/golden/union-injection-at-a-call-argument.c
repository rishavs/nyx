#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_take(nyx_sum_2 nyx_u_v)
{
    (void)nyx_u_v;
#line 2 "test.nip"
    if ((nyx_u_v.tag == 0)) {
#line 3 "test.nip"
        return (nyx_u_v.value.v0);
    }
#line 5 "test.nip"
    return 0;
}

#line 8 "test.nip"
int32_t nyx_u_pass(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 9 "test.nip"
    return nyx_u_take(((nyx_sum_2){ .tag = 0, .value.v0 = nyx_u_x }));
}

int32_t nyx_program_value(void)
{
#line 12 "test.nip"
    int32_t nyx_u_n = nyx_u_pass(3);
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
