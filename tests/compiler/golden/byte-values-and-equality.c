#include "test.h"

static inline bool nyx_eq_su(int64_t s, uint64_t u)
{ return s >= 0 && (uint64_t)s == u; }
static inline bool nyx_eq_us(uint64_t u, int64_t s)
{ return nyx_eq_su(s, u); }

int main(void)
{
#line 1 "test.nip"
    uint8_t nyx_u_b = 255;
    (void)nyx_u_b;
#line 2 "test.nip"
    nyx_sum_3 nyx_u_c = ((nyx_sum_3){ .tag = 1, .value.v1 = 7 });
    (void)nyx_u_c;
#line 3 "test.nip"
    bool nyx_u_ok = (nyx_eq_us(nyx_u_b, ((int64_t)255ULL)));
    (void)nyx_u_ok;
    return 0;
}
