/* The public compiler API: configuration limits, allocation failure, and the
   fallback diagnostic. */

#include "compile_support.h"

TEST("symbol interning recovers after index allocation failure")
{
    Ast ast;
    TypedProgram program;
    size_t i;
    char name[32];

    ast_init(&ast);
    typed_program_init(&program);
    for (i = 0; i < 16; i++) {
        snprintf(name, sizeof(name), "name%zu", i);
        if (ast_intern_symbol(&ast, name, strlen(name)) != i ||
            typed_program_copy_symbol(&program, name, strlen(name)) != i) {
            ast_free(&ast);
            typed_program_free(&program);
            return TEST_FAIL;
        }
    }
    nippy_alloc_fail_after(1);
    i = ast_intern_symbol(&ast, "new", 3);
    nippy_alloc_reset();
    if (i != AST_NO_NODE || ast_intern_symbol(&ast, "new", 3) != 16 ||
        ast_intern_symbol(&ast, "name0", 5) != 0) {
        ast_free(&ast);
        typed_program_free(&program);
        return TEST_FAIL;
    }
    nippy_alloc_fail_after(1);
    i = typed_program_copy_symbol(&program, "new", 3);
    nippy_alloc_reset();
    if (i != TYPED_NO_NODE || typed_program_copy_symbol(&program, "new", 3) != 16 ||
        typed_program_copy_symbol(&program, "name0", 5) != 0) {
        ast_free(&ast);
        typed_program_free(&program);
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TEST("compiler: source string is null")
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

TEST("compiler: injected out of memory")
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

TEST("compiler: custom Error capacities")
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

TEST("compiler: invalid Error capacity")
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

TEST("compiler: custom maximum string capacity")
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

TEST("compiler: maximum string capacity exceeded")
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

TEST("compiler: invalid maximum string capacity")
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

TEST("compiler: configured source limit")
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

TEST("compiler: raised maximum string capacity")
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

TEST("list: custom maximum capacity")
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

TEST("list: configured maximum exceeded")
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

TEST("list: invalid maximum capacity")
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

TEST("generated prefix")
{
    CompilerResult result = compiler_compile("var x: Int32 = 1\n",
                                             &test_config);
    int ok = result.succeeded && result.c_source != NULL &&
             strstr(result.c_source, "nyx_program_value") != NULL &&
             strstr(result.c_source, "nippy_") == NULL;

    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("print: custom maximum depth")
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

TEST("print: invalid maximum depth")
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

TEST("fallback: unknown error")
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
