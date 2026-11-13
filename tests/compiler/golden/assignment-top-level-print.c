#include "test.h"

#include <stdio.h>

int32_t nyx_program_value(void)
{
#line 1 "test.nip"
    int32_t nyx_u_x = 1;
#line 2 "test.nip"
    nyx_u_x = 2;
    return nyx_u_x;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
