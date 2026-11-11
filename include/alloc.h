#ifndef NIPPY_ALLOC_H
#define NIPPY_ALLOC_H

#include <stddef.h>

/* The compiler allocates through these wrappers so tests can force an
   allocation to fail and prove every out-of-memory path fails closed. In
   normal operation they are malloc/realloc with no extra state.

   `nippy_alloc_fail_after(n)` makes the next `n` allocations succeed and the
   one after that fail, after which the hook disables itself. Failing exactly
   one allocation at a time lets a test see the diagnostic that failure
   produces. Passing `NIPPY_ALLOC_NEVER` disables the hook. The hook is always
   compiled in: it costs one counter comparison and keeps the failure paths
   executable instead of dead code. */
/* "Never fail" sentinel for nippy_alloc_fail_after. */
#define NIPPY_ALLOC_NEVER ((size_t)-1)

void *nippy_malloc(size_t size);
void *nippy_realloc(void *pointer, size_t size);
char *nippy_strdup(const char *text);
void nippy_alloc_fail_after(size_t successful_allocations);
void nippy_alloc_reset(void);

#endif
