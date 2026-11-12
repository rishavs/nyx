#include "test_support.h"
#include "golden_data.h"


/* ---- unit tests for defensive / internal messages ---- */

TestResult test_compiler_source_is_null(void)
{
    CompilerResult result = compiler_compile(NULL, NULL);

    if (result.succeeded ||
        result.error_message == NULL ||
        !strstr(result.error_message, MESSAGE_COMPILER_SOURCE_IS_NULL)) {
        fprintf(stderr, "  compiler_compile(NULL) did not fail closed correctly\n");
        compiler_result_free(&result);
        return TEST_FAIL;
    }
    compiler_result_free(&result);
    return TEST_PASS;
}


TestResult test_compiler_out_of_memory_is_reported(void)
{
    CompilerResult result;
    CompilerConfig config = {.header_name = NULL,
                             .source_name = "example.nip"};

    nippy_alloc_fail_after(0);
    result = compiler_compile("var x : Int32 = 1\n", &config);
    nippy_alloc_reset();

    if (result.succeeded || result.error_message == NULL ||
        result.c_source != NULL) {
        fprintf(stderr, "  an injected allocation failure was not reported\n");
        compiler_result_free(&result);
        return TEST_FAIL;
    }
    compiler_result_free(&result);
    return TEST_PASS;
}




TestResult test_compiler_error_capacities(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .error_header_capacity = 8,
                             .error_description_capacity = 16,
                             .error_filename_capacity = 24};
    CompilerResult result = compiler_compile(
        "var e: Error = Error(\"E\", \"d\", \"f\", 1, 2, 3)\n", &config);
    int ok;

    ok = result.succeeded && result.h_source != NULL &&
         strstr(result.h_source, "nyx_string_8 header;") != NULL &&
         strstr(result.h_source, "nyx_string_16 description;") != NULL &&
         strstr(result.h_source, "nyx_string_24 filename;") != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  custom Error capacities were not used\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_error_capacity_invalid(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .error_header_capacity =
                                 NIPPY_MAX_STRING_CAPACITY + 1,
                             .error_description_capacity = 16,
                             .error_filename_capacity = 24};
    CompilerResult result = compiler_compile(
        "var e: Error = Error(\"E\", \"d\", \"f\", 1, 2, 3)\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_COMPILER_ERROR_CAPACITY, "header",
                   NIPPY_DEFAULT_MAX_STRING_CAPACITY);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  invalid Error capacity was not rejected\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}



TestResult test_compiler_max_string_capacity(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_string_capacity = 8};
    CompilerResult result = compiler_compile(
        "var s: String<8> = \"12345678\"\n", &config);
    int ok;

    ok = result.succeeded && result.h_source != NULL &&
         strstr(result.h_source, "nyx_string_8") != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  custom maximum string capacity was not used\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_max_string_exceeded(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_string_capacity = 8};
    CompilerResult result = compiler_compile(
        "var s: String<16> = \"hi\"\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_CHECKER_STRING_CAPACITY, 8);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr,
                "  Text capacity above the configured maximum was accepted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_max_string_invalid(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_string_capacity =
                                 NIPPY_MAX_STRING_CAPACITY + 1};
    CompilerResult result = compiler_compile(
        "var s: String<8> = \"hi\"\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_COMPILER_STRING_CAPACITY,
                   NIPPY_MAX_STRING_CAPACITY);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  invalid maximum string capacity was accepted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}



TestResult test_compiler_source_limit(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_source_bytes = 8};
    CompilerResult result = compiler_compile("var x: Int32 = 1\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_COMPILER_SOURCE_TOO_LARGE, 8);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  configured source limit was not enforced\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}



TestResult test_compiler_max_string_raised(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_string_capacity = 8192};
    CompilerResult result = compiler_compile(
        "var s: String<8192> = \"\"\n", &config);
    int ok;

    ok = result.succeeded && result.h_source != NULL &&
         strstr(result.h_source, "nyx_string_8192") != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  raised maximum string capacity was not used\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_max_list_capacity(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_list_capacity = 2};
    CompilerResult result = compiler_compile(
        "var xs: Array<Int32, 2> = [1, 2]\n", &config);
    int ok;

    ok = result.succeeded;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  custom maximum list capacity was not used\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_max_list_exceeded(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_list_capacity = 2};
    CompilerResult result = compiler_compile(
        "var xs: Array<Int32, 3> = [1, 2, 3]\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_CHECKER_ARRAY_CAPACITY, 2);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  list capacity above the configured maximum "
                        "was accepted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_invalid_max_list(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_list_capacity = NIPPY_MAX_LIST_CAPACITY + 1};
    CompilerResult result = compiler_compile("var n: Int32 = 1\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_COMPILER_LIST_CAPACITY, NIPPY_MAX_LIST_CAPACITY);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  invalid maximum list capacity was accepted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_generated_prefix(void)
{
    CompilerResult result = compiler_compile("var x: Int32 = 1\n",
                                             &test_config);
    int ok = result.succeeded && result.c_source != NULL &&
             strstr(result.c_source, "nyx_program_value") != NULL &&
             strstr(result.c_source, "nippy_") == NULL;

    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}


TestResult test_compiler_print_depth(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_print_depth = 3};
    CompilerResult result = compiler_compile("print(1)\n", &config);
    int ok = result.succeeded && result.c_source != NULL &&
             strstr(result.c_source, "depth > 3") != NULL;

    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  custom print depth was not emitted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}


TestResult test_compiler_invalid_print_depth(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_print_depth = -1};
    CompilerResult result = compiler_compile("print(1)\n", &config);
    char expected[256];
    int ok;

    expect_message(expected, sizeof(expected),
                   MESSAGE_COMPILER_PRINT_DEPTH, NIPPY_MAX_PRINT_DEPTH);
    ok = !result.succeeded && result.error_message != NULL &&
         strstr(result.error_message, expected) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  invalid print depth was accepted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

const TestCase compiler_api_test_tests[] = {
    {"compiler: source string is null", test_compiler_source_is_null},
    {"compiler: injected out of memory", test_compiler_out_of_memory_is_reported},
    {"generated prefix", test_compiler_generated_prefix},
    {"list: custom maximum capacity", test_compiler_max_list_capacity},
    {"print: custom maximum depth", test_compiler_print_depth},
    {"print: invalid maximum depth", test_compiler_invalid_print_depth},
    {"list: configured maximum exceeded", test_compiler_max_list_exceeded},
    {"list: invalid maximum capacity", test_compiler_invalid_max_list},
    {"compiler: custom Error capacities", test_compiler_error_capacities},
    {"compiler: invalid Error capacity", test_compiler_error_capacity_invalid},
    {"compiler: custom maximum string capacity", test_compiler_max_string_capacity},
    {"compiler: maximum string capacity exceeded", test_compiler_max_string_exceeded},
    {"compiler: invalid maximum string capacity", test_compiler_max_string_invalid},
    {"compiler: raised maximum string capacity", test_compiler_max_string_raised},
    {"compiler: configured source limit", test_compiler_source_limit},
};

const size_t compiler_api_test_test_count =
    sizeof(compiler_api_test_tests) / sizeof(compiler_api_test_tests[0]);
