#include "test.h"

#line 6 "test.nip"
nyx_u_nyx_gen_0 nyx_u_pick(nyx_sum_3 nyx_u_p)
{
    (void)nyx_u_p;
#line 7 "test.nip"
    if ((nyx_u_p.tag == 1)) {
#line 8 "test.nip"
        return (nyx_u_p.value.v1);
    }
#line 10 "test.nip"
    return (nyx_u_nyx_gen_0){ 0, 0 };
}

int main(void)
{
#line 13 "test.nip"
    nyx_u_nyx_gen_0 nyx_u_q = nyx_u_pick(((nyx_sum_3){ .tag = 1, .value.v1 = (nyx_u_nyx_gen_0){ 1, 2 } }));
    (void)nyx_u_q;
    return 0;
}
