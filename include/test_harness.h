#ifndef NIPPY_TEST_HARNESS_H
#define NIPPY_TEST_HARNESS_H

#include <stddef.h>

typedef enum {
    TEST_PASS,
    TEST_FAIL,
    TEST_TODO,
    TEST_ERROR
} TestResult;

typedef struct {
    const char *name;
    TestResult (*run)(void);
} TestCase;

/* Run every case in `tests`, print one line per case and a summary, and return
   0 when nothing failed or errored. */
int test_run_suite(const char *suite, const TestCase *tests, size_t count);

/* Render a MESSAGE_* format with its arguments into `buffer`. */
void expect_message(char *buffer, size_t size, const char *format, ...);

#endif
