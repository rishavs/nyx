#ifndef NYX_ALLOC_H
#define NYX_ALLOC_H

#include <stddef.h>

/* The compiler allocates through these wrappers so tests can force an
   allocation to fail and prove every out-of-memory path fails closed. In
   normal operation they are malloc/realloc with no extra state.

   `nyx_alloc_fail_after(n)` makes the next `n` allocations succeed and the
   one after that fail, after which the hook disables itself. Failing exactly
   one allocation at a time lets a test see the diagnostic that failure
   produces. Passing `NYX_ALLOC_NEVER` disables the hook. The hook is always
   compiled in: it costs one counter comparison and keeps the failure paths
   executable instead of dead code. */
/* "Never fail" sentinel for nyx_alloc_fail_after. */
#define NYX_ALLOC_NEVER ((size_t)-1)

void *nyx_malloc(size_t size);
void *nyx_realloc(void *pointer, size_t size);
char *nyx_strdup(const char *text);
void nyx_alloc_fail_after(size_t successful_allocations);
void nyx_alloc_reset(void);
/* Test instrumentation; counting is disabled unless explicitly enabled. */
void nyx_alloc_begin_counting(void);
size_t nyx_alloc_attempts(void);

#endif
