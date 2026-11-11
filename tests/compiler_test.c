#include "test_harness.h"
#include "alloc.h"
#include "analyser.h"
#include "ast.h"
#include "checker.h"
#include "compiler.h"
#include "error.h"
#include "generator.h"
#include "messages.h"
#include "typed_program.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const CompilerConfig test_config = {"test.h", "test.nip"};

static int error_contains(const ErrorState *errors, const char *needle)
{
    return strstr(errors->message, needle) != NULL;
}

static void report_mismatch(const char *what, const char *artifact,
                            const char *actual, const char *expected)
{
    size_t offset = 0;

    while (actual[offset] != '\0' && expected[offset] != '\0' &&
           actual[offset] == expected[offset]) {
        offset++;
    }
    fprintf(stderr, "  %s: generated %s differs at byte %zu\n",
            what, artifact, offset);
    fprintf(stderr, "    expected: %.48s\n", expected + offset);
    fprintf(stderr, "    actual:   %.48s\n", actual + offset);
}

/* Compile `source` in memory and compare both generated artifacts against the
   golden text the test owns. No files and no subprocess. */
static TestResult compile_succeeds(const char *source, const char *expected_c,
                                   const char *expected_h, const char *what)
{
    CompilerResult result = compiler_compile(source, &test_config);
    TestResult status = TEST_FAIL;

    if (!result.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                result.error_message == NULL ? "(no diagnostic)"
                                             : result.error_message);
        goto finished;
    }
    if (result.c_source == NULL || strcmp(result.c_source, expected_c) != 0) {
        report_mismatch(what, "C source",
                        result.c_source == NULL ? "" : result.c_source,
                        expected_c);
        goto finished;
    }
    if (result.h_source == NULL || strcmp(result.h_source, expected_h) != 0) {
        report_mismatch(what, "header",
                        result.h_source == NULL ? "" : result.h_source,
                        expected_h);
        goto finished;
    }
    status = TEST_PASS;

finished:
    compiler_result_free(&result);
    return status;
}

/* Compile `source`, expecting it to be rejected with a diagnostic that
   contains `format` rendered with `...`. Tests pass the MESSAGE_* macro itself
   so the assertion and the message cannot drift apart. */
static TestResult compile_fails(const char *source, const char *what,
                                const char *format, ...)
{
    CompilerResult result;
    char expected[256];
    va_list args;
    TestResult status = TEST_FAIL;

    va_start(args, format);
    vsnprintf(expected, sizeof(expected), format, args);
    va_end(args);

    result = compiler_compile(source, &test_config);
    if (result.succeeded) {
        fprintf(stderr, "  %s unexpectedly compiled\n", what);
        goto finished;
    }
    if (result.error_message == NULL ||
        strstr(result.error_message, expected) == NULL) {
        fprintf(stderr, "  %s: expected diagnostic containing \"%s\"\n",
                what, expected);
        if (result.error_message != NULL) {
            fprintf(stderr, "    actual: %s\n", result.error_message);
        }
        goto finished;
    }
    status = TEST_PASS;

finished:
    compiler_result_free(&result);
    return status;
}

/* ---- 13 ---- */
static const char golden_13_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    int32_t x = 13;\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_13_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- struct ---- */
static const char golden_struct_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    return 0;\n"
        "}\n";

static const char golden_struct_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    int32_t x;\n"
        "    int32_t y;\n"
        "} Point;\n"
        "\n"
        "#endif\n";

/* ---- values ---- */
static const char golden_values_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    Point p1 = (Point){ 1, 2 };\n"
        "    (void)p1;\n"
        "#line 7 \"test.nip\"\n"
        "    Point p2 = (Point){ 3, 4 };\n"
        "    (void)p2;\n"
        "#line 8 \"test.nip\"\n"
        "    Point p3 = (Point){ 6, 7 };\n"
        "    (void)p3;\n"
        "    return 0;\n"
        "}\n";

static const char golden_values_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    int32_t x;\n"
        "    int32_t y;\n"
        "} Point;\n"
        "\n"
        "#endif\n";

/* ---- function ---- */
static const char golden_function_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t adder(int32_t x, int32_t y)\n"
        "{\n"
        "    (void)x;\n"
        "    (void)y;\n"
        "#line 2 \"test.nip\"\n"
        "    return (int32_t)((uint32_t)(x) + (uint32_t)(y));\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t sum = adder(1, 2);\n"
        "    return sum;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_function_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t, int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- higher_order ---- */
static const char golden_higher_order_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t inc(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return (int32_t)((uint32_t)(x) + (uint32_t)(1));\n"
        "}\n"
        "\n"
        "#line 5 \"test.nip\"\n"
        "int32_t apply(nippy_fun_1 f, int32_t x)\n"
        "{\n"
        "    (void)f;\n"
        "    (void)x;\n"
        "#line 6 \"test.nip\"\n"
        "    return f(x);\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 9 \"test.nip\"\n"
        "    int32_t n = apply(inc, 41);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_higher_order_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "typedef int32_t (*nippy_fun_1)(int32_t);\n"
        "typedef int32_t (*nippy_fun_2)(nippy_fun_1, int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- function_value ---- */
static const char golden_function_value_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t add(int32_t x, int32_t y)\n"
        "{\n"
        "    (void)x;\n"
        "    (void)y;\n"
        "#line 2 \"test.nip\"\n"
        "    return (int32_t)((uint32_t)(x) + (uint32_t)(y));\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_fun_1 f = add;\n"
        "    (void)f;\n"
        "#line 6 \"test.nip\"\n"
        "    int32_t n = f(2, 3);\n"
        "    (void)n;\n"
        "    return 0;\n"
        "}\n";

static const char golden_function_value_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t, int32_t);\n"
        "typedef int32_t (*nippy_fun_1)(int32_t, int32_t);\n"
        "\n"
        "#endif\n";

/* ---- recursive ---- */
static const char golden_recursive_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t countdown(int32_t n)\n"
        "{\n"
        "    (void)n;\n"
        "#line 2 \"test.nip\"\n"
        "    return countdown(n);\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t zero = 0;\n"
        "    return zero;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_recursive_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- if_elseif_else ---- */
static const char golden_if_elseif_else_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t classify(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    if ((x > 0)) {\n"
        "#line 3 \"test.nip\"\n"
        "        return 1;\n"
        "    } else {\n"
        "#line 2 \"test.nip\"\n"
        "        if ((x == 0)) {\n"
        "#line 5 \"test.nip\"\n"
        "            return 2;\n"
        "        } else {\n"
        "#line 7 \"test.nip\"\n"
        "            return 3;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 11 \"test.nip\"\n"
        "    int32_t n = classify(5);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_if_elseif_else_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- while_break_continue ---- */
static const char golden_while_break_continue_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t loop(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    while (true) {\n"
        "#line 3 \"test.nip\"\n"
        "        if ((x > 0)) {\n"
        "#line 4 \"test.nip\"\n"
        "            break;\n"
        "        }\n"
        "#line 6 \"test.nip\"\n"
        "        continue;\n"
        "    }\n"
        "#line 8 \"test.nip\"\n"
        "    return 7;\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 11 \"test.nip\"\n"
        "    int32_t n = loop(1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_while_break_continue_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- logic ---- */
static const char golden_logic_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t choose(bool flag)\n"
        "{\n"
        "    (void)flag;\n"
        "#line 2 \"test.nip\"\n"
        "    if (flag) {\n"
        "#line 3 \"test.nip\"\n"
        "        return 1;\n"
        "    } else {\n"
        "#line 5 \"test.nip\"\n"
        "        return 0;\n"
        "    }\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 9 \"test.nip\"\n"
        "    int32_t n = choose((((!((1 == 2))) && (2 < 3)) || false));\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_logic_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(bool);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- bool_field ---- */
static const char golden_bool_field_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    Flags f = (Flags){ true, 3 };\n"
        "    (void)f;\n"
        "    return 0;\n"
        "}\n";

static const char golden_bool_field_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    bool on;\n"
        "    int32_t count;\n"
        "} Flags;\n"
        "\n"
        "#endif\n";

/* ---- oneline ---- */
static const char golden_oneline_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t f(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 1 \"test.nip\"\n"
        "    if ((x > 0)) {\n"
        "#line 1 \"test.nip\"\n"
        "        return 1;\n"
        "    } else {\n"
        "#line 1 \"test.nip\"\n"
        "        return 0;\n"
        "    }\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    int32_t n = f(1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_oneline_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- scattered ---- */
static const char golden_scattered_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t f(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 3 \"test.nip\"\n"
        "    if ((x > 0)) {\n"
        "#line 5 \"test.nip\"\n"
        "        return 1;\n"
        "    } else {\n"
        "#line 7 \"test.nip\"\n"
        "        return 0;\n"
        "    }\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 10 \"test.nip\"\n"
        "    int32_t n = f(1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_scattered_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- positive: each test owns its source and its expected generated C ---- */

TestResult test_13_codegen(void)
{
    return compile_succeeds("var x : Int32 = 13\n", golden_13_c, golden_13_h,
                            "program 13");
}

TestResult test_type_declaration_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n";

    return compile_succeeds(source, golden_struct_c, golden_struct_h,
                            "a type declaration");
}

TestResult test_struct_values_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "var p1: Point = Point(1, 2)\n"
        "var p2: Point = Point(x = 3, y = 4)\n"
        "var p3 = Point(6, 7)\n";

    return compile_succeeds(source, golden_values_c, golden_values_h,
                            "struct values and inference");
}

TestResult test_function_codegen(void)
{
    const char *source =
        "fun adder(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var sum : Int32 = adder(1, 2)\n";

    return compile_succeeds(source, golden_function_c, golden_function_h,
                            "a function declaration");
}

TestResult test_function_type_and_higher_order_codegen(void)
{
    const char *source =
        "fun inc(x:Int32):Int32\n"
        "  return x + 1\n"
        "end\n"
        "\n"
        "fun apply(f:Fun<(Int32):Int32>, x:Int32):Int32\n"
        "  return f(x)\n"
        "end\n"
        "\n"
        "var n : Int32 = apply(inc, 41)\n";

    return compile_succeeds(source, golden_higher_order_c,
                            golden_higher_order_h,
                            "a higher-order function");
}

TestResult test_function_value_codegen(void)
{
    const char *source =
        "fun add(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var f : Fun<(Int32, Int32):Int32> = add\n"
        "var n : Int32 = f(2, 3)\n";

    return compile_succeeds(source, golden_function_value_c,
                            golden_function_value_h,
                            "a function value and indirect call");
}

TestResult test_recursive_function_codegen(void)
{
    const char *source =
        "fun countdown(n:Int32):Int32\n"
        "  return countdown(n)\n"
        "end\n"
        "\n"
        "var zero : Int32 = 0\n";

    return compile_succeeds(source, golden_recursive_c, golden_recursive_h,
                            "a recursive function");
}

TestResult test_if_elseif_else_codegen(void)
{
    const char *source =
        "fun classify(x:Int32):Int32\n"
        "  if x > 0 then\n"
        "    return 1\n"
        "  elseif x == 0 then\n"
        "    return 2\n"
        "  else\n"
        "    return 3\n"
        "  end\n"
        "end\n"
        "\n"
        "var n : Int32 = classify(5)\n";

    return compile_succeeds(source, golden_if_elseif_else_c,
                            golden_if_elseif_else_h,
                            "if/elseif/else");
}

TestResult test_while_break_continue_codegen(void)
{
    const char *source =
        "fun loop(x:Int32):Int32\n"
        "  while true\n"
        "    if x > 0 then\n"
        "      break\n"
        "    end\n"
        "    continue\n"
        "  end\n"
        "  return 7\n"
        "end\n"
        "\n"
        "var n : Int32 = loop(1)\n";

    return compile_succeeds(source, golden_while_break_continue_c,
                            golden_while_break_continue_h,
                            "while with break and continue");
}

TestResult test_bool_logic_codegen(void)
{
    const char *source =
        "fun choose(flag:Bool):Int32\n"
        "  if flag then\n"
        "    return 1\n"
        "  else\n"
        "    return 0\n"
        "  end\n"
        "end\n"
        "\n"
        "var n : Int32 = choose(!(1 == 2) && (2 < 3) || false)\n";

    return compile_succeeds(source, golden_logic_c, golden_logic_h,
                            "Bool type and logical operators");
}

TestResult test_bool_struct_field_codegen(void)
{
    const char *source =
        "type Flags\n"
        "  on: Bool\n"
        "  count: Int32\n"
        "end\n"
        "\n"
        "var f : Flags = Flags(true, 3)\n";

    return compile_succeeds(source, golden_bool_field_c, golden_bool_field_h,
                            "a Bool struct field");
}

TestResult test_whitespace_agnostic_codegen(void)
{
    const char *single_line =
        "fun f(x:Int32):Int32 if x > 0 then return 1 else return 0 end end "
        "var n:Int32=f(1)\n";
    const char *scattered =
        "fun\tf(x:Int32):Int32\n\n"
        "if x>0\nthen\nreturn\t1\nelse\nreturn 0\nend\nend\n"
        "var\tn\t:\tInt32\t=\tf(\t1\t)\n";
    TestResult first = compile_succeeds(single_line, golden_oneline_c,
                                        golden_oneline_h,
                                        "a single-line program");
    TestResult second = compile_succeeds(scattered, golden_scattered_c,
                                         golden_scattered_h,
                                         "a whitespace-scattered program");

    return first == TEST_PASS && second == TEST_PASS ? TEST_PASS : TEST_FAIL;
}

/* ---- lexer messages ---- */

TestResult test_lexer_integer_out_of_range(void)
{
    return compile_fails("var x : Int32 = 99999999999999999999999999\n",
                             "an out-of-range integer literal",
                             MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
}

TestResult test_lexer_identifier_too_long(void)
{
    return compile_fails(
        "var abcdefghijklmnopqrstuvwxyzABCDEFG : Int32 = 1\n",
        "an over-long identifier",
        MESSAGE_LEXER_IDENTIFIER_TOO_LONG, NIPPY_IDENTIFIER_MAX - 1);
}

TestResult test_lexer_unexpected_character(void)
{
    return compile_fails("var x : Int32 = 13 @\n",
                             "an unexpected printable character",
                             MESSAGE_LEXER_UNEXPECTED_CHARACTER, '@');
}

TestResult test_lexer_unexpected_byte(void)
{
    return compile_fails("var x : Int32 = 13 \x80\n",
                             "an unexpected non-printable byte",
                             MESSAGE_LEXER_UNEXPECTED_BYTE, 0x80);
}

/* ---- parser messages ---- */

TestResult test_parser_expected_var_type_or_fun(void)
{
    return compile_fails("13\n", "a bare integer literal program",
                             MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
}

TestResult test_parser_expected_variable_name(void)
{
    return compile_fails("var : Int32 = 1\n", "a missing variable name",
                             MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}

TestResult test_parser_expected_type_name(void)
{
    return compile_fails("var x : = 1\n", "a missing type annotation",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME);
}

TestResult test_parser_expected_equals(void)
{
    return compile_fails("var x : Int32 13\n", "a declaration without '='",
                             MESSAGE_PARSER_EXPECTED_EQUALS);
}

TestResult test_parser_expected_expression(void)
{
    return compile_fails("var x : Int32 = +\n",
                             "an invalid initialiser expression",
                             MESSAGE_PARSER_EXPECTED_EXPRESSION);
}

TestResult test_parser_expected_type_name_after_type(void)
{
    return compile_fails("type 13 x: Int32 end\n",
                             "a type declaration without a name",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
}

TestResult test_parser_expected_at_least_one_field(void)
{
    return compile_fails("type Point end\n",
                             "a type declaration with no fields",
                             MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
}

TestResult test_parser_expected_field_name(void)
{
    return compile_fails("type Point 13 : Int32 end\n",
                             "a field without a name",
                             MESSAGE_PARSER_EXPECTED_FIELD_NAME);
}

TestResult test_parser_expected_field_colon(void)
{
    return compile_fails("type Point x Int32 end\n", "a field without ':'",
                             MESSAGE_PARSER_EXPECTED_FIELD_COLON);
}

TestResult test_parser_expected_field_type(void)
{
    return compile_fails("type Point x : 13 end\n", "a field without a type",
                             MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
}

TestResult test_parser_expected_field_or_end(void)
{
    return compile_fails("type Point x : Int32 13 end\n",
                             "a field not followed by another field or 'end'",
                             MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
}

TestResult test_parser_expected_argument(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(+)\n",
                             "a missing call argument",
                             MESSAGE_PARSER_EXPECTED_ARGUMENT);
}

TestResult test_parser_expected_comma_or_rparen(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(1 2)\n",
                             "constructor arguments without a separator",
                             MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN);
}

TestResult test_parser_mixed_args(void)
{
    return compile_fails("type P x: Int32 y: Int32 end\nvar p = P(1, y = 2)\n",
                             "mixed named and positional constructor arguments",
                             MESSAGE_PARSER_MIXED_ARGS);
}

TestResult test_parser_expected_function_name(void)
{
    return compile_fails("fun (x:Int32):Int32 return x end\n",
                             "a function without a name",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
}

TestResult test_parser_expected_function_lparen(void)
{
    return compile_fails("fun f x:Int32):Int32 return x end\n",
                             "a function without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN);
}

TestResult test_parser_expected_parameter_name(void)
{
    return compile_fails("fun f(:Int32):Int32 return 1 end\n",
                             "a parameter without a name",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_NAME);
}

TestResult test_parser_expected_parameter_colon(void)
{
    return compile_fails("fun f(x Int32):Int32 return x end\n",
                             "a parameter without ':'",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_COLON);
}

TestResult test_parser_expected_parameter_separator(void)
{
    return compile_fails("fun f(x:Int32 y:Int32):Int32 return x end\n",
                             "parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
}

TestResult test_parser_expected_return_type(void)
{
    return compile_fails("fun f(x:Int32) return x end\n",
                             "a function without a return type",
                             MESSAGE_PARSER_EXPECTED_RETURN_TYPE);
}

TestResult test_parser_expected_statement_or_end(void)
{
    return compile_fails("fun f(x:Int32):Int32 x end\n",
                             "a non-statement in a function body",
                             MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TestResult test_parser_expected_end(void)
{
    return compile_fails("fun f(x:Int32):Int32 return x\n",
                             "a function without 'end'",
                             MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TestResult test_parser_expected_then(void)
{
    return compile_fails("fun f(x:Int32):Int32 if x > 0 return 1 end end\n",
                             "an if without 'then'",
                             MESSAGE_PARSER_EXPECTED_THEN);
}

TestResult test_parser_expected_rparen(void)
{
    return compile_fails("var n : Int32 = (1 + 2\n",
                             "a grouped expression without ')'",
                             MESSAGE_PARSER_EXPECTED_RPAREN);
}

TestResult test_parser_expected_type(void)
{
    return compile_fails("fun f(x:):Int32 return x end\n",
                             "a missing parameter type",
                             MESSAGE_PARSER_EXPECTED_TYPE);
}

TestResult test_parser_expected_function_type_lparen(void)
{
    return compile_fails("var f : Fun<Int32> = 1\n",
                             "a function type without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN);
}

TestResult test_parser_expected_function_type_separator(void)
{
    return compile_fails("var f : Fun<(Int32 Int32):Int32> = 1\n",
                             "function type parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR);
}

TestResult test_parser_expected_function_type_colon(void)
{
    return compile_fails("var f : Fun<(Int32)> = 1\n",
                             "a function type without a return type colon",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_COLON);
}

TestResult test_parser_expected_function_type_gt(void)
{
    return compile_fails("var f : Fun<(Int32):Int32 = 1\n",
                             "a function type without '>'",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT);
}

/* ---- checker messages (user-reachable) ---- */

TestResult test_checker_unknown_type(void)
{
    return compile_fails("var x : Float = 13\n", "an unknown variable type",
                             MESSAGE_CHECKER_UNKNOWN_TYPE, "Float", "x");
}

TestResult test_checker_unknown_call(void)
{
    return compile_fails("var p = Foo(1, 2)\n",
                             "an unknown call",
                             MESSAGE_CHECKER_UNKNOWN_CALL, "Foo");
}

TestResult test_checker_constructor_not_struct(void)
{
    return compile_fails("var p = Int32(1)\n",
                             "constructing a non-struct type",
                             MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "Int32");
}

TestResult test_checker_constructor_arg_count(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(1, 2)\n",
                             "a constructor with the wrong argument count",
                             MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                             "P", (size_t)1, (size_t)2);
}

TestResult test_checker_missing_constructor_arg(void)
{
    return compile_fails("type P x: Int32 y: Int32 end\nvar p = P(x = 1)\n",
                             "a constructor missing a named argument",
                             MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG, "y");
}

TestResult test_checker_duplicate_constructor_arg(void)
{
    return compile_fails(
        "type P x: Int32 y: Int32 end\nvar p = P(x = 1, x = 2, y = 3)\n",
        "a constructor with a duplicate named argument",
        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG, "x");
}

TestResult test_checker_unknown_constructor_field(void)
{
    return compile_fails("type P x: Int32 end\nvar p = P(z = 1)\n",
                             "a constructor naming an unknown field",
                             MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD, "z");
}

TestResult test_checker_constructor_arg_type(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var p = P(f)\n",
        "a constructor argument with the wrong type",
        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE, (size_t)1, "P");
}

TestResult test_checker_initialiser_type_mismatch(void)
{
    return compile_fails("type P x: Int32 end\nvar p : P = 5\n",
                             "an initialiser whose type disagrees with the annotation",
                             MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "p", "P");
}

TestResult test_checker_duplicate_type(void)
{
    return compile_fails("type P x: Int32 end\ntype P y: Int32 end\n",
                             "a duplicated type declaration",
                             MESSAGE_CHECKER_DUPLICATE_TYPE, "P");
}

TestResult test_checker_redefines_int32(void)
{
    return compile_fails("type Int32 x: Int32 end\n",
                             "a type declaration named Int32",
                             MESSAGE_CHECKER_REDEFINES_INT32);
}

TestResult test_checker_unknown_name(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return y\nend\n",
        "an unknown name in a return expression",
        MESSAGE_CHECKER_UNKNOWN_NAME, "y");
}

TestResult test_checker_global_in_function(void)
{
    return compile_fails(
        "var g : Int32 = 1\n"
        "fun f(x:Int32):Int32\n  return g\nend\n",
        "a program-level variable used inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "g");
}

TestResult test_checker_type_as_value(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return P\nend\n",
        "a type used as a value",
        MESSAGE_CHECKER_TYPE_AS_VALUE, "P");
}

TestResult test_checker_not_callable(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = 1\n"
        "var z : Int32 = y(1)\n",
        "calling a non-function value",
        MESSAGE_CHECKER_NOT_CALLABLE, "y");
}

TestResult test_checker_named_function_arg(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(x = 1)\n",
        "a named function argument",
        MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
}

TestResult test_checker_function_arg_count(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(1, 2)\n",
        "a function call with the wrong argument count",
        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, "f", (size_t)1, (size_t)2);
}

TestResult test_checker_function_arg_type(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(p:P):Int32\n  return 1\nend\n"
        "var y : Int32 = f(1)\n",
        "a function call with the wrong argument type",
        MESSAGE_CHECKER_FUNCTION_ARG_TYPE, (size_t)1, "f");
}

TestResult test_checker_return_type_mismatch(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):P\n  return x\nend\n",
        "a return expression with the wrong type",
        MESSAGE_CHECKER_RETURN_TYPE_MISMATCH, "f");
}

TestResult test_checker_operator_type(void)
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(p:P):P\n  return p + p\nend\n",
        "an operator applied to a non-Int32 value",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TestResult test_checker_field_type(void)
{
    return compile_fails(
        "type Q x:Int32 end\ntype P y:Q end\n",
        "a non-Int32 field type",
        MESSAGE_CHECKER_FIELD_TYPE, "y");
}

TestResult test_checker_duplicate_name(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var f : Int32 = 1\n",
        "a duplicate top-level name",
        MESSAGE_CHECKER_DUPLICATE_NAME, "f");
}

TestResult test_checker_duplicate_parameter(void)
{
    return compile_fails(
        "fun f(x:Int32, x:Int32):Int32\n  return x\nend\n",
        "a duplicate parameter",
        MESSAGE_CHECKER_DUPLICATE_PARAMETER, "x");
}

TestResult test_checker_condition_type(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n"
        "  if x then\n    return 1\n  end\n"
        "  return 0\n"
        "end\n",
        "an Int32 if condition",
        MESSAGE_CHECKER_CONDITION_TYPE);
}

TestResult test_checker_break_outside_loop(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  break\n  return x\nend\n",
        "break outside a loop",
        MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP);
}

TestResult test_checker_continue_outside_loop(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  continue\n  return x\nend\n",
        "continue outside a loop",
        MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
}

TestResult test_checker_function_may_not_return(void)
{
    return compile_fails(
        "fun f(x:Int32):Int32\n"
        "  if x > 0 then\n    return 1\n  end\n"
        "end\n",
        "a function with a missing return path",
        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, "f");
}

TestResult test_checker_redefines_bool(void)
{
    return compile_fails("type Bool x:Int32 end\n",
                             "a type declaration named Bool",
                             MESSAGE_CHECKER_REDEFINES_BOOL);
}

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

TestResult test_checker_missing_ast_root(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_MISSING_AST_ROOT)) {
        fprintf(stderr, "  empty AST did not produce \"missing AST root\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_checker_missing_ast_node(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    size_t bad_root = 999;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    list_push(&ast.roots, &bad_root);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_MISSING_AST_NODE,
                   bad_root);
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  out-of-range root did not produce \"missing AST node\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_checker_unsupported_ast_node(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = (AstNodeKind)999;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_UNSUPPORTED_AST_NODE)) {
        fprintf(stderr,
                "  unknown node kind did not produce \"unsupported AST node\"\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_checker_no_initialiser(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_DECLARATION;
    node.name_index = ast_intern_symbol(&ast, "x", 1);
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_NO_INITIALISER,
                   "x");
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  missing initialiser did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_checker_missing_name_or_type(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_DECLARATION;
    node.name_index = AST_NO_NODE;
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_MISSING_NAME_OR_TYPE)) {
        fprintf(stderr, "  missing name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_checker_type_has_no_fields(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode node = {0};
    size_t root = 0;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);
    node.kind = AST_TYPE_DECLARATION;
    node.name_index = ast_intern_symbol(&ast, "Point", 5);
    node.left_child = AST_NO_NODE;
    node.right_child = AST_NO_NODE;
    node.children_start = 0;
    node.children_count = 0;
    list_push(&ast.nodes, &node);
    list_push(&ast.roots, &root);

    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, MESSAGE_CHECKER_TYPE_HAS_NO_FIELDS)) {
        fprintf(stderr, "  fieldless type did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_checker_missing_return(void)
{
    Ast ast;
    TypedProgram program;
    ErrorState errors;
    AstNode type_node = {0};
    AstNode function = {0};
    char expected[256];
    size_t function_index = 1;

    ast_init(&ast);
    typed_program_init(&program);
    error_init(&errors);

    type_node.kind = AST_TYPE_NAME;
    type_node.name_index = ast_intern_symbol(&ast, "Int32", 5);
    list_push(&ast.nodes, &type_node);

    function.kind = AST_FUNCTION_DECLARATION;
    function.name_index = ast_intern_symbol(&ast, "f", 1);
    function.left_child = AST_NO_NODE;
    function.right_child = 0;
    function.children_start = 0;
    function.children_count = 0;
    list_push(&ast.nodes, &function);
    list_push(&ast.roots, &function_index);

    expect_message(expected, sizeof(expected), MESSAGE_CHECKER_MISSING_RETURN,
                   "f");
    if (checker_check(&ast, &program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  a function without a return body was not rejected\n");
        ast_free(&ast);
        typed_program_free(&program);
        return TEST_FAIL;
    }
    ast_free(&ast);
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_root_out_of_range(void)
{
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    size_t bad_root = 999;

    typed_program_init(&program);
    error_init(&errors);
    list_push(&program.roots, &bad_root);

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_ROOT_OUT_OF_RANGE, bad_root);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  out-of-range root did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_node_link(void)
{
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    TypedNode node = {0};

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.left_child = 999;
    typed_program_push(&program, &node, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NODE_LINK, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid node link did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_name_index(void)
{
    TypedProgram program;
    ErrorState errors;
    char expected[256];
    TypedNode node = {0};

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.name_index = 999;
    typed_program_push(&program, &node, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_NAME_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid name index did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_type_index(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.type_index = 999;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    node.name_index = TYPED_NO_NODE;
    typed_program_push(&program, &node, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid type index did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_child_index(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    char expected[256];
    size_t bad_child = 999;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_DECLARATION;
    node.type_index = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    node.name_index = TYPED_NO_NODE;
    node.children_start = 0;
    node.children_count = 1;
    typed_program_push(&program, &node, &(size_t){0});
    typed_program_push_child(&program, bad_child);

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_CHILD_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid child index did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_signature_index(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    type.kind = NIPPY_TYPE_FUNCTION;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = 999;
    typed_program_add_type(&program, &type, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_SIGNATURE_INDEX, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid signature index did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_signature_link(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedFunctionSignature signature = {0};
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    signature.first_parameter = 0;
    signature.parameter_count = 0;
    signature.return_type = 999;
    typed_program_add_signature(&program, &signature, &(size_t){0});

    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_SIGNATURE_LINK, (size_t)0);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid signature link did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

static size_t push_int32_type(TypedProgram *program)
{
    TypedType type = {0};
    size_t index = 0;

    type.kind = NIPPY_TYPE_INT32;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    typed_program_add_type(program, &type, &index);
    return index;
}

static void make_int_var_program(TypedProgram *program)
{
    TypedNode literal = {0};
    TypedNode var = {0};
    size_t literal_index = 0;
    size_t var_index = 0;
    size_t name;
    size_t int32_type;

    typed_program_init(program);
    name = typed_program_copy_symbol(program, "x", 1);
    int32_type = push_int32_type(program);

    literal.kind = TYPED_INTEGER_LITERAL;
    literal.type_index = int32_type;
    literal.integer_value = 13;
    typed_program_push(program, &literal, &literal_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = int32_type;
    var.name_index = name;
    var.left_child = literal_index;
    typed_program_push(program, &var, &var_index);
    list_push(&program->roots, &var_index);
}

TestResult test_generator_unsupported_root(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode node = {0};
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    node.kind = TYPED_FIELD;
    typed_program_push(&program, &node, &(size_t){0});
    list_push(&program.roots, &(size_t){0});

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_ROOT)) {
        fprintf(stderr, "  unsupported root did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_unsupported_field(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode field = {0};
    TypedNode type_decl = {0};
    size_t field_index = 0;
    size_t decl_index = 0;
    size_t name;
    size_t int32_type;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "Point", 5);
    int32_type = push_int32_type(&program);

    field.kind = TYPED_INTEGER_LITERAL;
    field.type_index = int32_type;
    typed_program_push(&program, &field, &field_index);
    typed_program_push_child(&program, field_index);

    type_decl.kind = TYPED_TYPE_DECLARATION;
    type_decl.name_index = name;
    type_decl.children_start = 0;
    type_decl.children_count = 1;
    typed_program_push(&program, &type_decl, &decl_index);
    list_push(&program.roots, &decl_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_FIELD)) {
        fprintf(stderr, "  unsupported field did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_invalid_identifier(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode literal = {0};
    TypedNode var = {0};
    size_t literal_index = 0;
    size_t var_index = 0;
    size_t name;
    size_t int32_type;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "123", 3);
    int32_type = push_int32_type(&program);

    literal.kind = TYPED_INTEGER_LITERAL;
    literal.type_index = int32_type;
    literal.integer_value = 13;
    typed_program_push(&program, &literal, &literal_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = int32_type;
    var.name_index = name;
    var.left_child = literal_index;
    typed_program_push(&program, &var, &var_index);
    list_push(&program.roots, &var_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_INVALID_IDENTIFIER)) {
        fprintf(stderr, "  invalid identifier did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_invalid_header_name(void)
{
    TypedProgram program;
    ErrorState errors;
    char *c_source = NULL;
    char *h_source = NULL;

    make_int_var_program(&program);
    error_init(&errors);

    if (generator_generate(&program, "bad name", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_INVALID_HEADER_NAME)) {
        fprintf(stderr, "  invalid header name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_invalid_line_name(void)
{
    TypedProgram program;
    ErrorState errors;
    char *c_source = NULL;
    char *h_source = NULL;

    make_int_var_program(&program);
    error_init(&errors);

    if (generator_generate(&program, "ok.h", "bad\"name", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_INVALID_LINE_NAME)) {
        fprintf(stderr, "  invalid #line name did not produce its diagnostic\n");
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_unsupported_expression(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode bad = {0};
    TypedNode var = {0};
    size_t bad_index = 0;
    size_t var_index = 0;
    size_t name;
    size_t int32_type;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "x", 1);
    int32_type = push_int32_type(&program);

    bad.kind = TYPED_FIELD;
    bad.type_index = int32_type;
    typed_program_push(&program, &bad, &bad_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = int32_type;
    var.name_index = name;
    var.left_child = bad_index;
    typed_program_push(&program, &var, &var_index);
    list_push(&program.roots, &var_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_EXPRESSION)) {
        fprintf(stderr, "  unsupported expression did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_unsupported_function(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode function = {0};
    size_t function_index = 0;
    size_t name;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "f", 1);

    function.kind = TYPED_FUNCTION_DECLARATION;
    function.type_index = TYPED_NO_NODE;
    function.name_index = name;
    typed_program_push(&program, &function, &function_index);
    list_push(&program.roots, &function_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_FUNCTION)) {
        fprintf(stderr, "  unsupported function did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_unsupported_type(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode literal = {0};
    TypedNode var = {0};
    size_t literal_index = 0;
    size_t var_index = 0;
    size_t name;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "x", 1);

    literal.kind = TYPED_INTEGER_LITERAL;
    literal.type_index = 0;
    typed_program_push(&program, &literal, &literal_index);

    var.kind = TYPED_DECLARATION;
    var.type_index = 0;
    var.name_index = name;
    var.left_child = literal_index;
    typed_program_push(&program, &var, &var_index);
    list_push(&program.roots, &var_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_TYPE)) {
        fprintf(stderr, "  unsupported type did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_generator_unsupported_statement(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedNode bad = {0};
    TypedNode block = {0};
    TypedNode function = {0};
    TypedFunctionSignature signature = {0};
    TypedType function_type = {0};
    size_t bad_index = 0;
    size_t block_index = 0;
    size_t function_index = 0;
    size_t signature_index = 0;
    size_t function_type_index = 0;
    size_t int32_type;
    size_t name;
    char *c_source = NULL;
    char *h_source = NULL;

    typed_program_init(&program);
    error_init(&errors);
    int32_type = push_int32_type(&program);
    name = typed_program_copy_symbol(&program, "f", 1);

    signature.first_parameter = 0;
    signature.parameter_count = 0;
    signature.return_type = int32_type;
    typed_program_add_signature(&program, &signature, &signature_index);
    function_type.kind = NIPPY_TYPE_FUNCTION;
    function_type.name_index = TYPED_NO_NODE;
    function_type.signature_index = signature_index;
    typed_program_add_type(&program, &function_type, &function_type_index);

    bad.kind = TYPED_FIELD;
    bad.type_index = int32_type;
    bad.left_child = TYPED_NO_NODE;
    bad.right_child = TYPED_NO_NODE;
    bad.name_index = name;
    typed_program_push(&program, &bad, &bad_index);

    block.kind = TYPED_BLOCK;
    block.type_index = TYPED_NO_NODE;
    block.children_start = 0;
    block.children_count = 1;
    block.left_child = TYPED_NO_NODE;
    block.right_child = TYPED_NO_NODE;
    typed_program_push(&program, &block, &block_index);
    typed_program_push_child(&program, bad_index);

    function.kind = TYPED_FUNCTION_DECLARATION;
    function.type_index = function_type_index;
    function.name_index = name;
    function.left_child = block_index;
    function.right_child = TYPED_NO_NODE;
    function.children_start = 0;
    function.children_count = 0;
    typed_program_push(&program, &function, &function_index);
    list_push(&program.roots, &function_index);

    if (generator_generate(&program, "ok.h", "ok.nip", &c_source, &h_source,
                           &errors) ||
        !error_contains(&errors, MESSAGE_GENERATOR_UNSUPPORTED_STATEMENT)) {
        fprintf(stderr, "  unsupported statement did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_compiler_out_of_memory_is_reported(void)
{
    CompilerResult result;
    CompilerConfig config = {NULL, "example.nip"};

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

/* Every test in this suite, in run order. The table lives here so adding
   a test needs only its definition above and one entry below. */
static const TestCase tests[] = {
    {"var declaration of 13 generates the golden C", test_13_codegen},
    {"type declaration generates the golden C struct",
     test_type_declaration_codegen},
    {"struct values and inference generate the golden C",
     test_struct_values_codegen},
    {"function generates the golden C", test_function_codegen},
    {"function type and higher-order call generate the golden C",
     test_function_type_and_higher_order_codegen},
    {"function value and indirect call generate the golden C",
     test_function_value_codegen},
    {"recursive function generates the golden C", test_recursive_function_codegen},
    {"Bool and if/elseif/else generate the golden C", test_if_elseif_else_codegen},
    {"while with break and continue generate the golden C",
     test_while_break_continue_codegen},
    {"Bool type and logical operators generate the golden C",
     test_bool_logic_codegen},
    {"Bool struct field generates the golden C", test_bool_struct_field_codegen},
    {"newline and whitespace variants generate the golden C",
     test_whitespace_agnostic_codegen},
    {"lexer: integer literal out of range", test_lexer_integer_out_of_range},
    {"lexer: identifier too long", test_lexer_identifier_too_long},
    {"lexer: unexpected character", test_lexer_unexpected_character},
    {"lexer: unexpected byte", test_lexer_unexpected_byte},
    {"parser: expected var, type, or fun", test_parser_expected_var_type_or_fun},
    {"parser: expected variable name", test_parser_expected_variable_name},
    {"parser: expected type name", test_parser_expected_type_name},
    {"parser: expected equals", test_parser_expected_equals},
    {"parser: expected expression", test_parser_expected_expression},
    {"parser: expected type name after type",
     test_parser_expected_type_name_after_type},
    {"parser: expected at least one field",
     test_parser_expected_at_least_one_field},
    {"parser: expected field name", test_parser_expected_field_name},
    {"parser: expected field colon", test_parser_expected_field_colon},
    {"parser: expected field type", test_parser_expected_field_type},
    {"parser: expected field or end", test_parser_expected_field_or_end},
    {"parser: expected argument", test_parser_expected_argument},
    {"parser: expected comma or rparen", test_parser_expected_comma_or_rparen},
    {"parser: mixed named and positional args", test_parser_mixed_args},
    {"parser: expected function name", test_parser_expected_function_name},
    {"parser: expected function lparen", test_parser_expected_function_lparen},
    {"parser: expected parameter name", test_parser_expected_parameter_name},
    {"parser: expected parameter colon", test_parser_expected_parameter_colon},
    {"parser: expected parameter separator",
     test_parser_expected_parameter_separator},
    {"parser: expected return type", test_parser_expected_return_type},
    {"parser: expected statement or end", test_parser_expected_statement_or_end},
    {"parser: expected end", test_parser_expected_end},
    {"parser: expected then", test_parser_expected_then},
    {"parser: expected rparen", test_parser_expected_rparen},
    {"parser: expected type", test_parser_expected_type},
    {"parser: expected function type lparen",
     test_parser_expected_function_type_lparen},
    {"parser: expected function type separator",
     test_parser_expected_function_type_separator},
    {"parser: expected function type colon",
     test_parser_expected_function_type_colon},
    {"parser: expected function type gt", test_parser_expected_function_type_gt},
    {"checker: unknown type", test_checker_unknown_type},
    {"checker: unknown call", test_checker_unknown_call},
    {"checker: constructor of non-struct", test_checker_constructor_not_struct},
    {"checker: constructor argument count", test_checker_constructor_arg_count},
    {"checker: missing constructor argument", test_checker_missing_constructor_arg},
    {"checker: duplicate constructor argument",
     test_checker_duplicate_constructor_arg},
    {"checker: unknown constructor field", test_checker_unknown_constructor_field},
    {"checker: constructor argument type", test_checker_constructor_arg_type},
    {"checker: initialiser type mismatch", test_checker_initialiser_type_mismatch},
    {"checker: duplicate type", test_checker_duplicate_type},
    {"checker: redefines Int32", test_checker_redefines_int32},
    {"checker: unknown name", test_checker_unknown_name},
    {"checker: global in function", test_checker_global_in_function},
    {"checker: type as value", test_checker_type_as_value},
    {"checker: not callable", test_checker_not_callable},
    {"checker: named function argument", test_checker_named_function_arg},
    {"checker: function argument count", test_checker_function_arg_count},
    {"checker: function argument type", test_checker_function_arg_type},
    {"checker: return type mismatch", test_checker_return_type_mismatch},
    {"checker: operator type", test_checker_operator_type},
    {"checker: field type", test_checker_field_type},
    {"checker: duplicate name", test_checker_duplicate_name},
    {"checker: duplicate parameter", test_checker_duplicate_parameter},
    {"checker: condition type", test_checker_condition_type},
    {"checker: break outside loop", test_checker_break_outside_loop},
    {"checker: continue outside loop", test_checker_continue_outside_loop},
    {"checker: function may not return", test_checker_function_may_not_return},
    {"checker: redefines Bool", test_checker_redefines_bool},
    {"compiler: source string is null", test_compiler_source_is_null},
    {"fallback: unknown error", test_unknown_error_fallback},
    {"checker: missing AST root", test_checker_missing_ast_root},
    {"checker: missing AST node", test_checker_missing_ast_node},
    {"checker: unsupported AST node", test_checker_unsupported_ast_node},
    {"checker: no initialiser", test_checker_no_initialiser},
    {"checker: missing name or type", test_checker_missing_name_or_type},
    {"checker: type has no fields", test_checker_type_has_no_fields},
    {"checker: missing return", test_checker_missing_return},
    {"analyser: root out of range", test_analyser_root_out_of_range},
    {"analyser: invalid node link", test_analyser_invalid_node_link},
    {"analyser: invalid name index", test_analyser_invalid_name_index},
    {"analyser: invalid type index", test_analyser_invalid_type_index},
    {"analyser: invalid child index", test_analyser_invalid_child_index},
    {"analyser: invalid signature index", test_analyser_invalid_signature_index},
    {"analyser: invalid signature link", test_analyser_invalid_signature_link},
    {"generator: unsupported root", test_generator_unsupported_root},
    {"generator: unsupported field", test_generator_unsupported_field},
    {"generator: invalid identifier", test_generator_invalid_identifier},
    {"generator: invalid header name", test_generator_invalid_header_name},
    {"generator: invalid line name", test_generator_invalid_line_name},
    {"generator: unsupported expression", test_generator_unsupported_expression},
    {"generator: unsupported function", test_generator_unsupported_function},
    {"generator: unsupported type", test_generator_unsupported_type},
    {"generator: unsupported statement", test_generator_unsupported_statement},
    {"compiler: injected out of memory", test_compiler_out_of_memory_is_reported},
};

int main(void)
{
    return test_run_suite("compiler", tests,
                          sizeof(tests) / sizeof(tests[0]));
}
