#ifndef NIPPY_TEST_HARNESS_H
#define NIPPY_TEST_HARNESS_H

/* A small test runner.

   A test is a function defined with TEST(). It registers itself before main
   runs, so adding a test means writing it in one place: no table, no extern, no
   Makefile edit. The test's group is its source file's name, so tests/x/foo.c
   forms the group "foo". Every test runs in its own child process with its own
   scratch directory, so tests cannot disturb one another, a crash is reported
   against the test that caused it, and tests can run in parallel.

   Registration uses a GNU constructor attribute, which gcc and clang both
   accept under -std=c11 -Wpedantic. */

#include <stddef.h>

typedef enum {
    TEST_PASS,
    TEST_FAIL,
    TEST_TODO,
    TEST_ERROR
} TestResult;

typedef TestResult (*TestFn)(void);
typedef TestResult (*TestDataFn)(void *data);

void test_register(const char *file, int line, const char *name, TestFn run);
/* Registers a test that receives `data`, for tests discovered at startup (one
   per file in a directory, say) instead of written with TEST(). Call it from a
   constructor; the harness keeps `name` and `data`, so they must outlive main.
   Tests registered this way run in registration order within their group. */
void test_register_data(const char *file, int line, const char *name,
                        TestDataFn run, void *data);

#define NIPPY_TEST_CAT2(a, b) a##b
#define NIPPY_TEST_CAT(a, b) NIPPY_TEST_CAT2(a, b)

/* TEST("description") { ... return TEST_PASS; } */
#define TEST(description)                                                    \
    static TestResult NIPPY_TEST_CAT(nippy_test_, __LINE__)(void);           \
    static void NIPPY_TEST_CAT(nippy_register_, __LINE__)(void)              \
        __attribute__((constructor));                                        \
    static void NIPPY_TEST_CAT(nippy_register_, __LINE__)(void)              \
    {                                                                        \
        test_register(__FILE__, __LINE__, description,                       \
                      NIPPY_TEST_CAT(nippy_test_, __LINE__));                \
    }                                                                        \
    static TestResult NIPPY_TEST_CAT(nippy_test_, __LINE__)(void)

/* Runs the registered tests selected by the command line. `title` names the
   suite in the summary. Returns the process exit status. */
int test_main(int argc, char **argv, const char *title);

/* A path inside this test's private scratch directory. The directory is created
   before the test starts and removed after the run. The result is valid until
   the fourth later call. */
const char *test_path(const char *name);

/* Render a MESSAGE_* format with its arguments into `buffer`. */
void expect_message(char *buffer, size_t size, const char *format, ...);

#endif
