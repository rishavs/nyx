#include "test.h"

int main(void)
{
#line 1 "test.nip"
    uint8_t nyx_u_b = 255;
    (void)nyx_u_b;
#line 2 "test.nip"
    nyx_sum_3 nyx_u_c = ((nyx_sum_3){ .tag = 1, .value.v1 = 7 });
    (void)nyx_u_c;
#line 3 "test.nip"
    bool nyx_u_ok = (nyx_u_b == 255);
    (void)nyx_u_ok;
    return 0;
}
