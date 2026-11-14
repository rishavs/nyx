#include "test.h"

static inline bool nyx_eq_su(int64_t s, uint64_t u)
{ return s >= 0 && (uint64_t)s == u; }
static inline bool nyx_eq_us(uint64_t u, int64_t s)
{ return nyx_eq_su(s, u); }

#line 1 "test.nip"
bool nyx_u_is_seven(uint8_t nyx_u_b)
{
    (void)nyx_u_b;
#line 2 "test.nip"
    return (nyx_eq_us(nyx_u_b, ((int64_t)7ULL)));
}

int main(void)
{
#line 5 "test.nip"
    bool nyx_u_ok = nyx_u_is_seven(7);
    (void)nyx_u_ok;
    return 0;
}
