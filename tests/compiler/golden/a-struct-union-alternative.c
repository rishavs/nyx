#include "test.h"

#line 6 "test.nip"
nyx_u_nyx_gen_0 nyx_u_pick(nyx_sum_3 nyx_u_p)
{
    int32_t nyx_match_1;
    int32_t nyx_match_0;
    (void)nyx_u_p;
#line 7 "test.nip"
    if ((nyx_u_p.tag == 1)) {
#line 8 "test.nip"
        return (nyx_u_p.value.v1);
    }
#line 10 "test.nip"
    return (nyx_match_0 = 0, (nyx_match_1 = 0, (nyx_u_nyx_gen_0){ nyx_match_0, nyx_match_1 }));
}

int main(void)
{
    int32_t nyx_match_3;
    int32_t nyx_match_2;
#line 13 "test.nip"
    nyx_u_nyx_gen_0 nyx_u_q = nyx_u_pick(((nyx_sum_3){ .tag = 1, .value.v1 = (nyx_match_2 = 1, (nyx_match_3 = 2, (nyx_u_nyx_gen_0){ nyx_match_2, nyx_match_3 })) }));
    (void)nyx_u_q;
    return 0;
}
