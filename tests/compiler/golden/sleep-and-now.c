#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <time.h>

static inline int64_t nyx_clock_now(const char *file, size_t line, size_t col, size_t pos) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { fflush(stdout); fprintf(stderr, "nyx: %s:%zu:%zu (byte %zu): %s\n", file, line, col, pos, "monotonic clock is unavailable"); exit(1); }
    return (int64_t)ts.tv_sec * INT64_C(1000) + (int64_t)(ts.tv_nsec / 1000000);
}

static inline nyx_nil nyx_sleep(int64_t ms, const char *file, size_t line, size_t col, size_t pos) {
    struct timespec ts;
    if (ms < 0) { fflush(stdout); fprintf(stderr, "nyx: %s:%zu:%zu (byte %zu): %s\n", file, line, col, pos, "sleep duration cannot be negative"); exit(1); }
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
    return (nyx_nil)0;
}

static inline void nyx_print_5(bool value, int depth, const char *file, size_t line, size_t column, size_t pos);
static inline void nyx_print_5(bool value, int depth, const char *file, size_t line, size_t column, size_t pos)
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
#line 1 "test.nip"
    int64_t nyx_u_start = nyx_clock_now(__FILE__, 1, 20, 19);
    (void)nyx_u_start;
#line 2 "test.nip"
    (void)(nyx_sleep(((int64_t)10ULL), __FILE__, 2, 1, 25));
#line 3 "test.nip"
    {
        bool nyx_print_arg_35_0 = ((int64_t)((uint64_t)(nyx_clock_now(__FILE__, 3, 7, 41)) - (uint64_t)(nyx_u_start)) >= ((int64_t)10ULL));
        nyx_print_5(nyx_print_arg_35_0, 0, __FILE__, 3, 1, 35);
        fputc('\n', stdout);
    }
    return 0;
}
