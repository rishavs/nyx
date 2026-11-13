#include "test.h"

#line 1 "test.nip"
bool nyx_u_is_seven(uint8_t nyx_u_b)
{
    (void)nyx_u_b;
#line 2 "test.nip"
    return (nyx_u_b == 7);
}

int main(void)
{
#line 5 "test.nip"
    bool nyx_u_ok = nyx_u_is_seven(7);
    (void)nyx_u_ok;
    return 0;
}
