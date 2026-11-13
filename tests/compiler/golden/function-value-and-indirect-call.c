#include "test.h"

#line 1 "test.nip"
int32_t nyx_u_add(int32_t nyx_u_x, int32_t nyx_u_y)
{
    (void)nyx_u_x;
    (void)nyx_u_y;
#line 2 "test.nip"
    return (int32_t)((uint32_t)(nyx_u_x) + (uint32_t)(nyx_u_y));
}

int main(void)
{
#line 5 "test.nip"
    nyx_fun_1 nyx_u_f = nyx_u_add;
    (void)nyx_u_f;
#line 6 "test.nip"
    int32_t nyx_u_n = nyx_u_f(2, 3);
    (void)nyx_u_n;
    return 0;
}
