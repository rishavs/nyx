#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline bool nyx_equal_4(nyx_sum_4 a, nyx_sum_4 b, const char *file, size_t line, size_t column, size_t pos);
static inline bool nyx_equal_0(nyx_u_nyx_gen_0 a, nyx_u_nyx_gen_0 b, const char *file, size_t line, size_t column, size_t pos);
static inline bool nyx_equal_1(int32_t a, int32_t b, const char *file, size_t line, size_t column, size_t pos);
static inline bool nyx_equal_2(nyx_u_nyx_gen_1 a, nyx_u_nyx_gen_1 b, const char *file, size_t line, size_t column, size_t pos);
static inline bool nyx_equal_3(nyx_u_nyx_gen_2 a, nyx_u_nyx_gen_2 b, const char *file, size_t line, size_t column, size_t pos);
static inline bool nyx_equal_4(nyx_sum_4 a, nyx_sum_4 b, const char *file, size_t line, size_t column, size_t pos)
{
    (void)file; (void)line; (void)column; (void)pos;
    if (a.tag != b.tag) return false;
    switch (a.tag) {
    case 0: return nyx_equal_0(a.value.v0, b.value.v0, file, line, column, pos);
    case 1: return nyx_equal_2(a.value.v1, b.value.v1, file, line, column, pos);
    case 2: return nyx_equal_3(a.value.v2, b.value.v2, file, line, column, pos);
    default: return true;
    }
}

static inline bool nyx_equal_0(nyx_u_nyx_gen_0 a, nyx_u_nyx_gen_0 b, const char *file, size_t line, size_t column, size_t pos)
{
    (void)file; (void)line; (void)column; (void)pos;
    if (!nyx_equal_1(a.nyx_u_radius, b.nyx_u_radius, file, line, column, pos)) return false;
    return true;
}

static inline bool nyx_equal_1(int32_t a, int32_t b, const char *file, size_t line, size_t column, size_t pos)
{
    (void)file; (void)line; (void)column; (void)pos;
    return a == b;
}

static inline bool nyx_equal_2(nyx_u_nyx_gen_1 a, nyx_u_nyx_gen_1 b, const char *file, size_t line, size_t column, size_t pos)
{
    (void)file; (void)line; (void)column; (void)pos;
    if (!nyx_equal_1(a.nyx_u_side, b.nyx_u_side, file, line, column, pos)) return false;
    return true;
}

static inline bool nyx_equal_3(nyx_u_nyx_gen_2 a, nyx_u_nyx_gen_2 b, const char *file, size_t line, size_t column, size_t pos)
{
    (void)file; (void)line; (void)column; (void)pos;
    (void)a; (void)b;
    return true;
}

static inline void nyx_print_7(bool value, int depth, const char *file, size_t line, size_t column, size_t pos);
static inline void nyx_print_7(bool value, int depth, const char *file, size_t line, size_t column, size_t pos)
{
    (void)value; (void)file; (void)line; (void)column; (void)pos;
    if (depth > 16) {
        fputs("<...>", stdout);
        return;
    }
    fputs(value ? "true" : "false", stdout);
}


int main(void)
{
#line 6 "test.nip"
    nyx_sum_4 nyx_u_a = ((nyx_sum_4){ .tag = 0, .value.v0 = (nyx_u_nyx_gen_0){ 3 } });
    (void)nyx_u_a;
#line 7 "test.nip"
    nyx_sum_4 nyx_u_b = ((nyx_sum_4){ .tag = 2, .value.v2 = (nyx_u_nyx_gen_2){ 0 } });
    (void)nyx_u_b;
#line 8 "test.nip"
    {
        bool nyx_print_arg_161_0 = (nyx_equal_4(nyx_u_a, nyx_u_b, __FILE__, 8, 9, 169));
        nyx_print_7(nyx_print_arg_161_0, 0, __FILE__, 8, 1, 161);
        fputc('\n', stdout);
    }
    return 0;
}
