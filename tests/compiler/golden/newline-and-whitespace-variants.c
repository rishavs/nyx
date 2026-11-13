#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_f(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 3 "test.nip"
    if ((nyx_u_x > 0)) {
#line 5 "test.nip"
        return 1;
    } else {
#line 7 "test.nip"
        return 0;
    }
}

int32_t nyx_program_value(void)
{
#line 10 "test.nip"
    int32_t nyx_u_n = nyx_u_f(1);
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
