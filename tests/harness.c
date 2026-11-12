#include "test_harness.h"

#include <stdarg.h>
#include <stdio.h>

void expect_message(char *buffer, size_t size, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, size, format, args);
    va_end(args);
}

int test_run_suite(const char *suite, const TestCase *tests, size_t count)
{
    size_t passed = 0;
    size_t failed = 0;
    size_t todo = 0;
    size_t errors = 0;
    size_t i;

    printf("%s suite\n", suite);
    for (i = 0; i < count; i++) {
        TestResult result = tests[i].run();

        switch (result) {
        case TEST_PASS:
            printf("PASS: %s\n", tests[i].name);
            passed++;
            break;
        case TEST_FAIL:
            fprintf(stderr, "FAIL: %s\n", tests[i].name);
            failed++;
            break;
        case TEST_TODO:
            printf("TODO: %s\n", tests[i].name);
            todo++;
            break;
        case TEST_ERROR:
        default:
            fprintf(stderr, "ERROR: %s\n", tests[i].name);
            errors++;
            break;
        }
    }

    printf("%zu tests: %zu PASS, %zu FAIL, %zu TODO, %zu Error\n",
           count, passed, failed, todo, errors);
    return failed == 0 && errors == 0 ? 0 : 1;
}

int test_run_suites(const char *suite, const TestSuite *suites, size_t count)
{
    size_t passed = 0;
    size_t failed = 0;
    size_t todo = 0;
    size_t errors = 0;
    size_t total = 0;
    size_t i;

    printf("%s suite\n", suite);
    for (i = 0; i < count; i++) {
        size_t j;

        for (j = 0; j < suites[i].count; j++) {
            const TestCase *test = &suites[i].tests[j];
            TestResult result = test->run();

            total++;
            switch (result) {
            case TEST_PASS:
                printf("PASS: %s\n", test->name);
                passed++;
                break;
            case TEST_FAIL:
                fprintf(stderr, "FAIL: %s\n", test->name);
                failed++;
                break;
            case TEST_TODO:
                printf("TODO: %s\n", test->name);
                todo++;
                break;
            case TEST_ERROR:
            default:
                fprintf(stderr, "ERROR: %s\n", test->name);
                errors++;
                break;
            }
        }
    }

    printf("%zu tests: %zu PASS, %zu FAIL, %zu TODO, %zu Error\n",
           total, passed, failed, todo, errors);
    return failed == 0 && errors == 0 ? 0 : 1;
}
