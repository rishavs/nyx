#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_loop(int32_t nyx_u_x)
{
    (void)nyx_u_x;
#line 2 "test.nip"
    while (true) {
#line 3 "test.nip"
        if ((nyx_u_x > 0)) {
#line 4 "test.nip"
            break;
        }
#line 6 "test.nip"
        continue;
    }
#line 8 "test.nip"
    return 7;
}

int32_t nyx_program_value(void)
{
#line 11 "test.nip"
    int32_t nyx_u_n = nyx_u_loop(1);
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
