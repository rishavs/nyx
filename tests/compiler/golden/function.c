#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_adder(int32_t nyx_u_x, int32_t nyx_u_y)
{
    (void)nyx_u_x;
    (void)nyx_u_y;
#line 2 "test.nip"
    return (int32_t)((uint32_t)(nyx_u_x) + (uint32_t)(nyx_u_y));
}

int32_t nyx_program_value(void)
{
#line 5 "test.nip"
    int32_t nyx_u_sum = nyx_u_adder(1, 2);
    return nyx_u_sum;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
