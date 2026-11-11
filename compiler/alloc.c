#include "alloc.h"

#include <stdlib.h>
#include <string.h>

/* Countdown to the first allocation that must fail. SIZE_MAX means "never". */
static size_t allocations_remaining = (size_t)-1;

void nippy_alloc_fail_after(size_t successful_allocations)
{
    allocations_remaining = successful_allocations;
}

void nippy_alloc_reset(void)
{
    allocations_remaining = (size_t)-1;
}

/* Returns true when this allocation should fail. Exactly one allocation
   fails, then the hook disables itself: isolating one failure at a time lets
   the suite observe the message that failure produces, which a cascading
   failure would mask (the error-message copy would fail too). */
static int allocation_should_fail(void)
{
    if (allocations_remaining == (size_t)-1) {
        return 0;
    }
    if (allocations_remaining == 0) {
        allocations_remaining = (size_t)-1;
        return 1;
    }
    allocations_remaining--;
    return 0;
}

void *nippy_malloc(size_t size)
{
    if (allocation_should_fail()) {
        return NULL;
    }
    return malloc(size);
}

void *nippy_realloc(void *pointer, size_t size)
{
    if (allocation_should_fail()) {
        return NULL;
    }
    return realloc(pointer, size);
}

char *nippy_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    length = strlen(text) + 1;
    copy = nippy_malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}
