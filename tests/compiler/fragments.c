/* The words a message substitutes into itself, and the placeholders a program
   prints, live in messages.h as MESSAGE_FRAGMENT_* and MESSAGE_RUNTIME_PRINT_*.
   These tests name them. */

#include "compile_support.h"

TEST("diagnostics: the position prefix and an unnamed source")
{
    ErrorState errors;
    SourcePos pos = {7, 2, 3};
    char expected[128];

    error_init(&errors);
    snprintf(expected, sizeof(expected), MESSAGE_FRAGMENT_POSITION_PREFIX,
             MESSAGE_FRAGMENT_UNNAMED_SOURCE, (size_t)2, (size_t)3, (size_t)7);
    error_report_at(&errors, pos, MESSAGE_UNKNOWN_ERROR);
    if (strncmp(errors.message, expected, strlen(expected)) != 0) {
        fprintf(stderr, "  expected the prefix \"%s\" in \"%s\"\n", expected,
                errors.message);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("diagnostics: an operator error names a function and a string literal")
{
    TestResult function = compile_fails(
        "fun f(): Int32\n  return 1\nend\nvar a: Int32 = 1\nvar b = a + f\n",
        "a function operand", MESSAGE_CHECKER_OPERATOR_TYPES, "+", "Int32",
        MESSAGE_FRAGMENT_FUNCTION);

    if (function != TEST_PASS) return function;
    return compile_fails("var c = \"x\" + 1\n", "a string literal operand",
                         MESSAGE_CHECKER_OPERATOR_TYPES, "+",
                         MESSAGE_FRAGMENT_STRING_LITERAL, "Int64");
}

TEST("Error values default their file name when the source has none")
{
    CompilerConfig config = {.header_name = "test.h"};
    CompilerResult result = compiler_compile(
        "var e: Error = Error::throw(\"E\", \"d\")\n", &config);
    int ok = result.succeeded && result.c_source != NULL &&
             strstr(result.c_source, MESSAGE_FRAGMENT_DEFAULT_SOURCE_NAME) !=
                 NULL;

    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  the default source name was not used\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("print: the placeholders a program prints are emitted")
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip",
                             .max_print_depth = 1};
    CompilerResult result = compiler_compile(
        "global root: Store = Store(64)\n"
        "var h: Handle<Int32> = root.alloc<Int32>(1)\n"
        "type A x: Int32 end\n"
        "type B a: A end\n"
        "type C b: B end\n"
        "var c: C = C(B(A(1)))\n"
        "print(h, c)\n",
        &config);
    int ok = result.succeeded && result.c_source != NULL &&
             strstr(result.c_source, MESSAGE_RUNTIME_PRINT_TRUNCATED) != NULL;

    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  the truncation placeholder was not emitted\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}
