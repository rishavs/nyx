/* now() and sleep(): checker rules, typed-program validation, and the C the
   generator emits for them. */

#include "compile_support.h"

TEST("now takes no arguments")
{
    return compile_fails("var t = now(1)\n", "now with an argument",
                         MESSAGE_CHECKER_NOW_ARGUMENTS);
}

TEST("sleep requires exactly one argument")
{
    TestResult none = compile_fails("sleep()\n", "sleep without an argument",
                                    MESSAGE_CHECKER_SLEEP_ARGUMENTS);

    if (none != TEST_PASS) return none;
    return compile_fails("sleep(1, 2)\n", "sleep with two arguments",
                         MESSAGE_CHECKER_SLEEP_ARGUMENTS);
}

TEST("sleep requires an Int64 duration")
{
    TestResult narrow = compile_fails("var n: Int32 = 5\nsleep(n)\n",
                                      "sleep with an Int32",
                                      MESSAGE_CHECKER_SLEEP_ARGUMENTS);
    TestResult real;

    if (narrow != TEST_PASS) return narrow;
    real = compile_fails("sleep(1.5)\n", "sleep with a float",
                         MESSAGE_CHECKER_SLEEP_ARGUMENTS);
    if (real != TEST_PASS) return real;
    return compile_fails("sleep(true)\n", "sleep with a Bool",
                         MESSAGE_CHECKER_SLEEP_ARGUMENTS);
}

TEST("sleep rejects a negative literal duration")
{
    return compile_fails("sleep(-5)\n", "negative sleep",
                         MESSAGE_CHECKER_SLEEP_NEGATIVE);
}

TEST("sleep accepts zero, a literal, and an Int64 expression")
{
    return compile_accepts("var d: Int64 = 2\n"
                           "sleep(0)\n"
                           "sleep(-0)\n"
                           "sleep(1)\n"
                           "sleep(d * 3)\n",
                           "sleep durations");
}

TEST("now is an Int64")
{
    TestResult ok = compile_accepts("var t: Int64 = now()\n"
                                    "var later: Int64 = now() - t\n",
                                    "now as an Int64");

    if (ok != TEST_PASS) return ok;
    return compile_fails("var t: Int32 = now()\n", "now as an Int32",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "t",
                         "Int32");
}

TEST("now and sleep are reserved names")
{
    if (compile_fails("fun now(): Int64 return 1 end\n", "function now",
                      MESSAGE_CHECKER_BUILTIN_RESERVED, "now") != TEST_PASS) {
        return TEST_FAIL;
    }
    return compile_fails("fun sleep(ms: Int64) end\n", "function sleep",
                         MESSAGE_CHECKER_BUILTIN_RESERVED, "sleep");
}

/* The analyser validates typed-program links, so a malformed clock node built
   by hand must be rejected rather than reach the generator. */
static void typed_clock_setup(TypedProgram *program, ErrorState *errors,
                              size_t *int64_type, size_t *nil_type)
{
    TypedType type = {0};

    typed_program_init(program);
    error_init(errors);
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.kind = NYX_TYPE_INT64;
    typed_program_add_type(program, &type, int64_type);
    type.kind = NYX_TYPE_NIL;
    typed_program_add_type(program, &type, nil_type);
}

static TypedNode typed_clock_node(TypedNodeKind kind, size_t type_index)
{
    TypedNode node = {0};

    node.kind = kind;
    node.type_index = type_index;
    node.name_index = TYPED_NO_NODE;
    node.auxiliary_type = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    return node;
}

TEST("now with the wrong type is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t int64_type, nil_type, index;
    TypedNode node;
    char expected[256];
    bool ok;

    typed_clock_setup(&program, &errors, &int64_type, &nil_type);
    node = typed_clock_node(TYPED_CLOCK_NOW, nil_type);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("now with an operand is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t int64_type, nil_type, literal, index;
    TypedNode node;
    char expected[256];
    bool ok;

    typed_clock_setup(&program, &errors, &int64_type, &nil_type);
    node = typed_clock_node(TYPED_INTEGER_LITERAL, int64_type);
    if (!typed_program_push(&program, &node, &literal)) return TEST_ERROR;
    node = typed_clock_node(TYPED_CLOCK_NOW, int64_type);
    node.left_child = literal;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("sleep without an Int64 operand is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t int64_type, nil_type, index;
    TypedNode node;
    char expected[256];
    bool ok;

    typed_clock_setup(&program, &errors, &int64_type, &nil_type);
    node = typed_clock_node(TYPED_SLEEP, nil_type);
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("sleep with the wrong result type is rejected")
{
    TypedProgram program;
    ErrorState errors;
    size_t int64_type, nil_type, literal, index;
    TypedNode node;
    char expected[256];
    bool ok;

    typed_clock_setup(&program, &errors, &int64_type, &nil_type);
    node = typed_clock_node(TYPED_INTEGER_LITERAL, int64_type);
    if (!typed_program_push(&program, &node, &literal)) return TEST_ERROR;
    node = typed_clock_node(TYPED_SLEEP, int64_type);
    node.left_child = literal;
    if (!typed_program_push(&program, &node, &index)) return TEST_ERROR;
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, index);
    ok = !analyser_analyse(&program, NULL, &errors) &&
         error_contains(&errors, expected);
    typed_program_free(&program);
    return ok ? TEST_PASS : TEST_FAIL;
}

TEST("the POSIX feature macro and helpers appear only when the clock is used")
{
    CompilerResult slept = compiler_compile("sleep(1)\n", &test_config);
    CompilerResult read = compiler_compile("var t: Int64 = now()\n",
                                           &test_config);
    CompilerResult unused = compiler_compile("var n: Int32 = 1\n", &test_config);
    const char *macro = "#define _POSIX_C_SOURCE";
    int ok;

    ok = slept.succeeded && read.succeeded && unused.succeeded &&
         strncmp(slept.c_source, macro, strlen(macro)) == 0 &&
         strstr(slept.c_source, "nyx_sleep") != NULL &&
         strstr(slept.c_source, "nyx_clock_now") == NULL &&
         strncmp(read.c_source, macro, strlen(macro)) == 0 &&
         strstr(read.c_source, "nyx_clock_now") != NULL &&
         strstr(read.c_source, "nyx_sleep") == NULL &&
         strstr(unused.c_source, macro) == NULL &&
         strstr(unused.c_source, "nyx_sleep") == NULL &&
         strstr(unused.c_source, "nyx_clock_now") == NULL;
    compiler_result_free(&slept);
    compiler_result_free(&read);
    compiler_result_free(&unused);
    return ok ? TEST_PASS : TEST_FAIL;
}
