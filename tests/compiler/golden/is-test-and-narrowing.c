#include "test.h"

#include <stdio.h>

#line 1 "test.nip"
int32_t nyx_u_classify(nyx_sum_3 nyx_u_x)
{
    (void)nyx_u_x;
#line 2 "test.nip"
    if ((nyx_u_x.tag == 0)) {
#line 3 "test.nip"
        return (nyx_u_x.value.v0);
    } else {
#line 2 "test.nip"
        if ((((nyx_u_x.tag == 1) ? ((nyx_sum_5){ .tag = 0, .value.v0 = (nyx_u_x.value.v1) }) : ((nyx_sum_5){ .tag = 1 })).tag == 0)) {
#line 5 "test.nip"
            if ((nyx_u_x.value.v1)) {
#line 6 "test.nip"
                return 1;
            } else {
#line 8 "test.nip"
                return 0;
            }
        } else {
#line 11 "test.nip"
            return 0;
        }
    }
}

int32_t nyx_program_value(void)
{
#line 15 "test.nip"
    int32_t nyx_u_n = nyx_u_classify(((nyx_sum_3){ .tag = 0, .value.v0 = 3 }));
    return nyx_u_n;
}

int main(void)
{
    printf("%d\n", (int)nyx_program_value());
    return 0;
}
