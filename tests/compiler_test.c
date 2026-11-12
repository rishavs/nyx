#include "test_support.h"

/* Test suite tables are defined by the split test modules. */
extern const TestCase golden_test_1_tests[];
extern const size_t golden_test_1_test_count;
extern const TestCase golden_test_2_tests[];
extern const size_t golden_test_2_test_count;
extern const TestCase syntax_test_1_tests[];
extern const size_t syntax_test_1_test_count;
extern const TestCase checker_test_1_tests[];
extern const size_t checker_test_1_test_count;
extern const TestCase checker_test_2_tests[];
extern const size_t checker_test_2_test_count;
extern const TestCase checker_test_3_tests[];
extern const size_t checker_test_3_test_count;
extern const TestCase compiler_api_test_tests[];
extern const size_t compiler_api_test_test_count;
extern const TestCase misc_test_tests[];
extern const size_t misc_test_test_count;
extern const TestCase analyser_test_tests[];
extern const size_t analyser_test_test_count;
extern const TestCase generator_test_tests[];
extern const size_t generator_test_test_count;

int main(void)
{
    TestSuite suites[] = {
        {"golden_test_1", golden_test_1_tests, golden_test_1_test_count},
        {"golden_test_2", golden_test_2_tests, golden_test_2_test_count},
        {"syntax_test_1", syntax_test_1_tests, syntax_test_1_test_count},
        {"checker_test_1", checker_test_1_tests, checker_test_1_test_count},
        {"checker_test_2", checker_test_2_tests, checker_test_2_test_count},
        {"checker_test_3", checker_test_3_tests, checker_test_3_test_count},
        {"compiler_api_test", compiler_api_test_tests, compiler_api_test_test_count},
        {"misc_test", misc_test_tests, misc_test_test_count},
        {"analyser_test", analyser_test_tests, analyser_test_test_count},
        {"generator_test", generator_test_tests, generator_test_test_count},
    };

    return test_run_suites("compiler", suites,
                           sizeof(suites) / sizeof(suites[0]));
}
