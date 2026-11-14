#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_choose(bool nyx_u_flag)
{
    (void)nyx_u_flag;
#line 2 "test.nip"
    if (nyx_u_flag) {
#line 3 "test.nip"
        return 1;
    } else {
#line 5 "test.nip"
        return 0;
    }
}

int32_t nyx_program_value(void)
{
#line 9 "test.nip"
    int32_t nyx_u_n = nyx_u_choose((((!((((int64_t)1ULL) == ((int64_t)2ULL)))) && (((int64_t)2ULL) < ((int64_t)3ULL))) || false));
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
