#include "tool_support.h"

/* Test suite tables are defined by the split tool-test modules. */
extern const TestCase runtime_print_test_tests[];
extern const size_t runtime_print_test_test_count;
extern const TestCase runtime_language_test_tests[];
extern const size_t runtime_language_test_test_count;
extern const TestCase runtime_trap_test_tests[];
extern const size_t runtime_trap_test_test_count;
extern const TestCase driver_test_tests[];
extern const size_t driver_test_test_count;
extern const TestCase runtime_store_test_tests[];
extern const size_t runtime_store_test_test_count;
extern const TestCase runtime_string_test_tests[];
extern const size_t runtime_string_test_test_count;
extern const TestCase runtime_collection_test_tests[];
extern const size_t runtime_collection_test_test_count;

int main(void)
{
    TestSuite suites[] = {
        {"runtime_print_test", runtime_print_test_tests, runtime_print_test_test_count},
        {"runtime_language_test", runtime_language_test_tests,
         runtime_language_test_test_count},
        {"runtime_trap_test", runtime_trap_test_tests,
         runtime_trap_test_test_count},
        {"driver_test", driver_test_tests, driver_test_test_count},
        {"runtime_store_test", runtime_store_test_tests, runtime_store_test_test_count},
        {"runtime_string_test", runtime_string_test_tests,
         runtime_string_test_test_count},
        {"runtime_collection_test", runtime_collection_test_tests,
         runtime_collection_test_test_count},
    };

    return test_run_suites("tool", suites,
                           sizeof(suites) / sizeof(suites[0]));
}
