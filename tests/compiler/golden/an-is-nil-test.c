#include "test.h"

#line 1 "test.nip"
bool nyx_u_is_nil(nyx_sum_2 nyx_u_x)
{
    (void)nyx_u_x;
#line 2 "test.nip"
    if ((nyx_u_x.tag == 1)) {
#line 3 "test.nip"
        return true;
    }
#line 5 "test.nip"
    return false;
}

int main(void)
{
    return 0;
}
