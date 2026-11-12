#include "test_support.h"
#include "golden_data.h"


TestResult test_unknown_error_fallback(void)
{
    ErrorState errors;

    error_init(&errors);
    error_unknown_if_none(&errors);
    if (!errors.reported || !error_contains(&errors, MESSAGE_UNKNOWN_ERROR)) {
        fprintf(stderr, "  fallback diagnostic was not produced\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

const TestCase misc_test_tests[] = {
    {"fallback: unknown error", test_unknown_error_fallback},
};

const size_t misc_test_test_count =
    sizeof(misc_test_tests) / sizeof(misc_test_tests[0]);
