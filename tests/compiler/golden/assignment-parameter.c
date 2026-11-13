#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_bump(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 2 "test.nip"
    nyx_u_x = (int32_t)((uint32_t)(nyx_u_x) + (uint32_t)(1));
#line 3 "test.nip"
    return nyx_u_x;
}

int32_t nyx_program_value(void)
{
#line 6 "test.nip"
    int32_t nyx_u_n = nyx_u_bump(1);
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
