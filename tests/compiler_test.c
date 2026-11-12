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

static const CompilerConfig test_config = {.header_name = "test.h",
                                           .source_name = "test.nip"};

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

/* A large golden is split into several C string literals so no single literal
   exceeds the ISO C minimum. */
typedef struct {
    const char *const *parts;
    size_t count;
} GoldenText;

static size_t text_mismatch_offset(const char *actual, const GoldenText *golden)
{
    size_t offset = 0;
    size_t i;

    for (i = 0; i < golden->count; i++) {
        const char *part = golden->parts[i];
        size_t j;

        for (j = 0; part[j] != '\0'; j++) {
            if (actual[offset] != part[j]) {
                return offset;
            }
            offset++;
        }
    }
    return actual[offset] == '\0' ? (size_t)-1 : offset;
}

static TestResult compile_succeeds_parts(const char *source,
                                         const GoldenText *expected_c,
                                         const char *expected_h,
                                         const char *what)
{
    CompilerResult result = compiler_compile(source, &test_config);
    TestResult status = TEST_FAIL;
    size_t offset;

    if (!result.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                result.error_message == NULL ? "(no diagnostic)"
                                             : result.error_message);
        goto finished;
    }
    offset = result.c_source == NULL
                 ? 0
                 : text_mismatch_offset(result.c_source, expected_c);
    if (result.c_source == NULL || offset != (size_t)-1) {
        fprintf(stderr,
                "  %s: generated C source differs at byte %zu\n", what,
                offset);
        goto finished;
    }
    if (result.h_source == NULL || strcmp(result.h_source, expected_h) != 0) {
        fprintf(stderr, "  %s: generated header does not match the golden\n",
                what);
        goto finished;
    }
    status = TEST_PASS;

finished:
    compiler_result_free(&result);
    return status;
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
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- values ---- */
static const char golden_values_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    nippy_gen_0 p1 = (nippy_gen_0){ 1, 2 };\n"
        "    (void)p1;\n"
        "#line 7 \"test.nip\"\n"
        "    nippy_gen_0 p2 = (nippy_gen_0){ 3, 4 };\n"
        "    (void)p2;\n"
        "#line 8 \"test.nip\"\n"
        "    nippy_gen_0 p3 = (nippy_gen_0){ 6, 7 };\n"
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
        "} nippy_gen_0;\n"
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
        "    nippy_gen_0 f = (nippy_gen_0){ true, 3 };\n"
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
        "} nippy_gen_0;\n"
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

/* ---- nil_return ---- */
static const char golden_nil_return_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "nippy_nil nothing(void)\n"
        "{\n"
        "#line 2 \"test.nip\"\n"
        "    return ((nippy_nil)0);\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    return 0;\n"
        "}\n";

static const char golden_nil_return_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef nippy_nil (*nippy_fun_0)(void);\n"
        "\n"
        "#endif\n";

/* ---- union_vars ---- */
static const char golden_union_vars_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 3 \"test.nip\"\n"
        "    nippy_sum_3 a = ((nippy_sum_3){ .tag = 0, .value.v0 = 3 });\n"
        "    (void)a;\n"
        "#line 4 \"test.nip\"\n"
        "    nippy_sum_3 b = ((nippy_sum_3){ .tag = 1, .value.v1 = true });\n"
        "    (void)b;\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_sum_3 c = ((nippy_sum_3){ .tag = 2 });\n"
        "    (void)c;\n"
        "    return 0;\n"
        "}\n";

static const char golden_union_vars_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "        bool v1;\n"
        "    } value;\n"
        "} nippy_sum_3;\n"
        "\n"
        "#endif\n";

/* ---- union_canonical ---- */
static const char golden_union_canonical_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    nippy_sum_2 a = ((nippy_sum_2){ .tag = 0, .value.v0 = 3 });\n"
        "    (void)a;\n"
        "#line 2 \"test.nip\"\n"
        "    nippy_sum_2 b = ((nippy_sum_2){ .tag = 1, .value.v1 = false });\n"
        "    (void)b;\n"
        "    return 0;\n"
        "}\n";

static const char golden_union_canonical_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "        bool v1;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "#endif\n";

/* ---- is_narrow ---- */
static const char golden_is_narrow_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t classify(nippy_sum_3 x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    if ((x.tag == 0)) {\n"
        "#line 3 \"test.nip\"\n"
        "        return (x.value.v0);\n"
        "    } else {\n"
        "#line 2 \"test.nip\"\n"
        "        if ((x.tag == 1)) {\n"
        "#line 5 \"test.nip\"\n"
        "            if ((x.value.v1)) {\n"
        "#line 6 \"test.nip\"\n"
        "                return 1;\n"
        "            } else {\n"
        "#line 8 \"test.nip\"\n"
        "                return 0;\n"
        "            }\n"
        "        } else {\n"
        "#line 11 \"test.nip\"\n"
        "            return 0;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 15 \"test.nip\"\n"
        "    int32_t n = classify(((nippy_sum_3){ .tag = 0, .value.v0 = 3 }));\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_is_narrow_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "        bool v1;\n"
        "    } value;\n"
        "} nippy_sum_3;\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(nippy_sum_3);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- generic_alias ---- */
static const char golden_generic_alias_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 3 \"test.nip\"\n"
        "    nippy_sum_2 a = ((nippy_sum_2){ .tag = 0, .value.v0 = 3 });\n"
        "    (void)a;\n"
        "#line 4 \"test.nip\"\n"
        "    nippy_sum_2 b = ((nippy_sum_2){ .tag = 1 });\n"
        "    (void)b;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_alias_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "#endif\n";

/* ---- generic_struct ---- */
static const char golden_generic_struct_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    nippy_gen_0 p = (nippy_gen_0){ 1, true };\n"
        "    (void)p;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_struct_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    int32_t first;\n"
        "    bool second;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- generic_function ---- */
static const char golden_generic_function_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t nippy_genfn_0(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t n = nippy_genfn_0(3);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_function_h[] =
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

/* ---- generic_two_types ---- */
static const char golden_generic_two_types_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t nippy_genfn_0(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "bool nippy_genfn_1(bool x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t a = nippy_genfn_0(1);\n"
        "    (void)a;\n"
        "#line 6 \"test.nip\"\n"
        "    bool b = nippy_genfn_1(true);\n"
        "    (void)b;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_two_types_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "typedef bool (*nippy_fun_1)(bool);\n"
        "\n"
        "#endif\n";

/* ---- generic_hof ---- */
static const char golden_generic_hof_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 5 \"test.nip\"\n"
        "int32_t inc(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 6 \"test.nip\"\n"
        "    return (int32_t)((uint32_t)(x) + (uint32_t)(1));\n"
        "}\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t nippy_genfn_0(nippy_fun_3 f, int32_t x)\n"
        "{\n"
        "    (void)f;\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return f(x);\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 9 \"test.nip\"\n"
        "    int32_t n = nippy_genfn_0(inc, 1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_hof_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "typedef int32_t (*nippy_fun_1)(int32_t);\n"
        "typedef int32_t (*nippy_fun_2)(nippy_fun_1, int32_t);\n"
        "typedef int32_t (*nippy_fun_3)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- generic_nested ---- */
static const char golden_generic_nested_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 7 \"test.nip\"\n"
        "    nippy_sum_2 o = ((nippy_sum_2){ .tag = 0, .value.v0 = 3 });\n"
        "    (void)o;\n"
        "#line 8 \"test.nip\"\n"
        "    nippy_gen_0 p = (nippy_gen_0){ o, false };\n"
        "    (void)p;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_nested_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "#line 2 \"test.nip\"\n"
        "typedef struct {\n"
        "    nippy_sum_2 first;\n"
        "    bool second;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- generic_self ---- */
static const char golden_generic_self_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t nippy_genfn_0(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return nippy_genfn_0(x);\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t n = nippy_genfn_0(1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_self_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "typedef int32_t (*nippy_fun_1)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- union_field ---- */
static const char golden_union_field_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_gen_0 h = (nippy_gen_0){ ((nippy_sum_2){ .tag = 1 }) };\n"
        "    (void)h;\n"
        "    return 0;\n"
        "}\n";

static const char golden_union_field_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    nippy_sum_2 value;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- is_static ---- */
static const char golden_is_static_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "bool yes(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return true;\n"
        "}\n"
        "\n"
        "#line 5 \"test.nip\"\n"
        "bool no(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 6 \"test.nip\"\n"
        "    return false;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    return 0;\n"
        "}\n";

static const char golden_is_static_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef bool (*nippy_fun_0)(int32_t);\n"
        "typedef bool (*nippy_fun_1)(int32_t);\n"
        "\n"
        "#endif\n";

/* ---- is_nil ---- */
static const char golden_is_nil_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "bool is_nil(nippy_sum_2 x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    if ((x.tag == 1)) {\n"
        "#line 3 \"test.nip\"\n"
        "        return true;\n"
        "    }\n"
        "#line 5 \"test.nip\"\n"
        "    return false;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    return 0;\n"
        "}\n";

static const char golden_is_nil_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "typedef bool (*nippy_fun_0)(nippy_sum_2);\n"
        "\n"
        "#endif\n";

/* ---- union_struct ---- */
static const char golden_union_struct_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 6 \"test.nip\"\n"
        "nippy_gen_0 pick(nippy_sum_3 p)\n"
        "{\n"
        "    (void)p;\n"
        "#line 7 \"test.nip\"\n"
        "    if ((p.tag == 1)) {\n"
        "#line 8 \"test.nip\"\n"
        "        return (p.value.v1);\n"
        "    }\n"
        "#line 10 \"test.nip\"\n"
        "    return (nippy_gen_0){ 0, 0 };\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 13 \"test.nip\"\n"
        "    nippy_gen_0 q = pick(((nippy_sum_3){ .tag"
        " = 1, .value.v1 = (nippy_gen_0){ 1, 2 } }));\n"
        "    (void)q;\n"
        "    return 0;\n"
        "}\n";

static const char golden_union_struct_h[] =
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
        "} nippy_gen_0;\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        nippy_gen_0 v1;\n"
        "    } value;\n"
        "} nippy_sum_3;\n"
        "\n"
        "typedef nippy_gen_0 (*nippy_fun_0)(nippy_sum_3);\n"
        "\n"
        "#endif\n";

/* ---- generic_struct_two ---- */
static const char golden_generic_struct_two_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    nippy_gen_0 a = (nippy_gen_0){ 1, true };\n"
        "    (void)a;\n"
        "#line 7 \"test.nip\"\n"
        "    nippy_gen_1 b = (nippy_gen_1){ true, 1 };\n"
        "    (void)b;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_struct_two_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    int32_t first;\n"
        "    bool second;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    bool first;\n"
        "    int32_t second;\n"
        "} nippy_gen_1;\n"
        "\n"
        "#endif\n";

/* ---- generic_struct_arg ---- */
static const char golden_generic_struct_arg_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 6 \"test.nip\"\n"
        "nippy_gen_0 nippy_genfn_1(nippy_gen_0 x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 7 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 10 \"test.nip\"\n"
        "    nippy_gen_0 p = nippy_genfn_1((nippy_gen_0){ 1, 2 });\n"
        "    (void)p;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_struct_arg_h[] =
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
        "} nippy_gen_0;\n"
        "\n"
        "typedef nippy_gen_0 (*nippy_fun_0)(nippy_gen_0);\n"
        "\n"
        "#endif\n";

/* ---- generic_two_params ---- */
static const char golden_generic_two_params_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t nippy_genfn_0(int32_t a, bool b)\n"
        "{\n"
        "    (void)a;\n"
        "    (void)b;\n"
        "#line 2 \"test.nip\"\n"
        "    return a;\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t n = nippy_genfn_0(1, true);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_two_params_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t, bool);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- generic_nested_call ---- */
static const char golden_generic_nested_call_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t nippy_genfn_1(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "#line 5 \"test.nip\"\n"
        "int32_t nippy_genfn_0(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 6 \"test.nip\"\n"
        "    return nippy_genfn_1(x);\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 9 \"test.nip\"\n"
        "    int32_t n = nippy_genfn_0(3);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_nested_call_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(int32_t);\n"
        "typedef int32_t (*nippy_fun_1)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- inject_call ---- */
static const char golden_inject_call_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t take(nippy_sum_2 v)\n"
        "{\n"
        "    (void)v;\n"
        "#line 2 \"test.nip\"\n"
        "    if ((v.tag == 0)) {\n"
        "#line 3 \"test.nip\"\n"
        "        return (v.value.v0);\n"
        "    }\n"
        "#line 5 \"test.nip\"\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "#line 8 \"test.nip\"\n"
        "int32_t pass(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 9 \"test.nip\"\n"
        "    return take(((nippy_sum_2){ .tag = 0, .value.v0 = x }));\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 12 \"test.nip\"\n"
        "    int32_t n = pass(3);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_inject_call_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(nippy_sum_2);\n"
        "typedef int32_t (*nippy_fun_1)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- union_field_int ---- */
static const char golden_union_field_int_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_gen_0 h = (nippy_gen_0){"
        " ((nippy_sum_2){ .tag = 0, .value.v0 = 3 }) };\n"
        "    (void)h;\n"
        "    return 0;\n"
        "}\n";

static const char golden_union_field_int_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    nippy_sum_2 value;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- generic_struct_named ---- */
static const char golden_generic_struct_named_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    nippy_gen_0 p = (nippy_gen_0){ 1, true };\n"
        "    (void)p;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_struct_named_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    int32_t first;\n"
        "    bool second;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- generic_struct_point ---- */
static const char golden_generic_struct_point_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 11 \"test.nip\"\n"
        "    nippy_gen_1 p = (nippy_gen_1){ (nippy_gen_0){ 1, 2 }, true };\n"
        "    (void)p;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_struct_point_h[] =
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
        "} nippy_gen_0;\n"
        "\n"
        "#line 6 \"test.nip\"\n"
        "typedef struct {\n"
        "    nippy_gen_0 first;\n"
        "    bool second;\n"
        "} nippy_gen_1;\n"
        "\n"
        "#endif\n";

/* ---- alias_chain ---- */
static const char golden_alias_chain_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 4 \"test.nip\"\n"
        "    nippy_sum_4 v = ((nippy_sum_4){ .tag = 1, .value.v1 = true });\n"
        "    (void)v;\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_sum_4 n = ((nippy_sum_4){ .tag = 0, .value.v0 = 3 });\n"
        "    (void)n;\n"
        "    return 0;\n"
        "}\n";

static const char golden_alias_chain_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "        bool v1;\n"
        "    } value;\n"
        "} nippy_sum_4;\n"
        "\n"
        "#endif\n";

/* ---- generic_union_return ---- */
static const char golden_generic_union_return_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "nippy_sum_2 nippy_genfn_0(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    return ((nippy_sum_2){ .tag = 0, .value.v0 = x });\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_sum_2 m = nippy_genfn_0(3);\n"
        "    (void)m;\n"
        "    return 0;\n"
        "}\n";

static const char golden_generic_union_return_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        int32_t v0;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "typedef nippy_sum_2 (*nippy_fun_0)(int32_t);\n"
        "\n"
        "#endif\n";

/* ---- nil_field_param ---- */
static const char golden_nil_field_param_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 5 \"test.nip\"\n"
        "int32_t take(nippy_nil n)\n"
        "{\n"
        "    (void)n;\n"
        "#line 6 \"test.nip\"\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 9 \"test.nip\"\n"
        "    nippy_gen_0 m = (nippy_gen_0){ ((nippy_nil)0) };\n"
        "    (void)m;\n"
        "#line 10 \"test.nip\"\n"
        "    int32_t n = take(((nippy_nil)0));\n"
        "    (void)n;\n"
        "    return 0;\n"
        "}\n";

static const char golden_nil_field_param_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    nippy_nil value;\n"
        "} nippy_gen_0;\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(nippy_nil);\n"
        "\n"
        "#endif\n";

/* ---- store_alloc ---- */
static const char *const golden_store_alloc_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 16);\n"
        "#line 2 \"test.nip\"\n"
        "    nippy_handle p = nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)p;\n"
        "#line 3 \"test.nip\"\n"
        "    int32_t n = (*(int32_t *)nippy_handle_pointer(p, sizeof(int32_t)));\n"
        "    (void)n;\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_store_alloc_c = {
    golden_store_alloc_c_parts, 2
};

static const char golden_store_alloc_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "#endif\n";

/* ---- store_write_reset ---- */
static const char *const golden_store_write_reset_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 16);\n"
        "#line 2 \"test.nip\"\n"
        "    nippy_handle p = nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)p;\n"
        "#line 3 \"test.nip\"\n"
        "    nippy_nil w = ((void)nippy_handle_write(p,"
        " &(int32_t){ 2 }, sizeof(int32_t)), (nippy_nil)0);\n"
        "    (void)w;\n"
        "#line 4 \"test.nip\"\n"
        "    nippy_nil r = ((void)nippy_store_reset(&nippy_stores[0]), (nippy_nil)0);\n"
        "    (void)r;\n",
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_store_write_reset_c = {
    golden_store_write_reset_c_parts, 3
};

static const char golden_store_write_reset_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "#endif\n";

/* ---- byte_basics ---- */
static const char golden_byte_basics_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    uint8_t b = 255;\n"
        "    (void)b;\n"
        "#line 2 \"test.nip\"\n"
        "    nippy_sum_3 c = ((nippy_sum_3){ .tag = 1, .value.v1 = 7 });\n"
        "    (void)c;\n"
        "#line 3 \"test.nip\"\n"
        "    bool ok = (b == 255);\n"
        "    (void)ok;\n"
        "    return 0;\n"
        "}\n";

static const char golden_byte_basics_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        uint8_t v1;\n"
        "    } value;\n"
        "} nippy_sum_3;\n"
        "\n"
        "#endif\n";

/* ---- byte_struct ---- */
static const char golden_byte_struct_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    nippy_gen_0 b = (nippy_gen_0){ 200 };\n"
        "    (void)b;\n"
        "    return 0;\n"
        "}\n";

static const char golden_byte_struct_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    uint8_t value;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- handle_field ---- */
static const char *const golden_handle_field_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 16);\n"
        "#line 6 \"test.nip\"\n"
        "    nippy_handle p = nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)p;\n"
        "#line 7 \"test.nip\"\n"
        "    nippy_gen_0 holder = (nippy_gen_0){ p };\n"
        "    (void)holder;\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_handle_field_c = {
    golden_handle_field_c_parts, 2
};

static const char golden_handle_field_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "typedef struct {\n"
        "    nippy_handle h;\n"
        "} nippy_gen_0;\n"
        "\n"
        "#endif\n";

/* ---- store_functions ---- */
static const char *const golden_store_functions_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "#line 3 \"test.nip\"\n"
        "nippy_handle make(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 4 \"test.nip\"\n"
        "    return nippy_store_alloc(&nippy_stores[0], 0,"
        " &(int32_t){ x }, sizeof(int32_t), _Alignof(int32_t));\n"
        "}\n"
        "\n"
        "#line 7 \"test.nip\"\n"
        "int32_t read_it(nippy_handle h)\n"
        "{\n"
        "    (void)h;\n"
        "#line 8 \"test.nip\"\n"
        "    return (*(int32_t *)nippy_handle_pointer(h, sizeof(int32_t)));\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n",
        "#line 11 \"test.nip\"\n"
        "    int32_t n = read_it(make(42));\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 16);\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_store_functions_c = {
    golden_store_functions_c_parts, 3
};

static const char golden_store_functions_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef nippy_handle (*nippy_fun_0)(int32_t);\n"
        "typedef int32_t (*nippy_fun_1)(nippy_handle);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- expression_statement ---- */
static const char *const golden_expression_statement_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "#line 3 \"test.nip\"\n"
        "nippy_nil clear(void)\n"
        "{\n"
        "#line 4 \"test.nip\"\n"
        "    (void)(((void)nippy_store_reset(&nippy_stores[0]), (nippy_nil)0));\n"
        "#line 5 \"test.nip\"\n"
        "    return ((nippy_nil)0);\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 16);\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_expression_statement_c = {
    golden_expression_statement_c_parts, 2
};

static const char golden_expression_statement_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef nippy_nil (*nippy_fun_0)(void);\n"
        "\n"
        "#endif\n";

/* ---- top_level_reset ---- */
static const char *const golden_top_level_reset_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 16);\n"
        "#line 2 \"test.nip\"\n"
        "    (void)(((void)nippy_store_reset(&nippy_stores[0]), (nippy_nil)0));\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_top_level_reset_c = {
    golden_top_level_reset_c_parts, 2
};

static const char golden_top_level_reset_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "#endif\n";

/* ---- byte_equality ---- */
static const char golden_byte_equality_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "bool is_seven(uint8_t b)\n"
        "{\n"
        "    (void)b;\n"
        "#line 2 \"test.nip\"\n"
        "    return (b == 7);\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    bool ok = is_seven(7);\n"
        "    (void)ok;\n"
        "    return 0;\n"
        "}\n";

static const char golden_byte_equality_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef bool (*nippy_fun_0)(uint8_t);\n"
        "\n"
        "#endif\n";

/* ---- alloc_byte ---- */
static const char *const golden_alloc_byte_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 1);\n"
        "#line 2 \"test.nip\"\n"
        "    nippy_handle b = nippy_store_alloc(&nippy_stores[0], 0,"
        " &(uint8_t){ 255 }, sizeof(uint8_t), _Alignof(uint8_t));\n"
        "    (void)b;\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_alloc_byte_c = {
    golden_alloc_byte_c_parts, 2
};

static const char golden_alloc_byte_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "#endif\n";

/* ---- two_stores ---- */
static const char *const golden_two_stores_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 2\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 1);\n"
        "    nippy_store_init(&nippy_stores[1], 1);\n"
        "#line 3 \"test.nip\"\n"
        "    nippy_handle p = nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)p;\n"
        "#line 4 \"test.nip\"\n"
        "    nippy_handle q = nippy_store_alloc(&nippy_stores[1],"
        " 1, &(int32_t){ 2 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)q;\n"
        "#line 5 \"test.nip\"\n",
        "    int32_t n = (int32_t)((uint32_t)((*(int32_t *)nippy_handle_pointer(p,"
        " sizeof(int32_t)))) + (uint32_t)((*(int32_t"
        " *)nippy_handle_pointer(q, sizeof(int32_t)))));\n"
        "    (void)n;\n"
        "#line 6 \"test.nip\"\n"
        "    (void)(((void)nippy_store_reset(&nippy_stores[0]), (nippy_nil)0));\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    nippy_store_free(&nippy_stores[1]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_two_stores_c = {
    golden_two_stores_c_parts, 3
};

static const char golden_two_stores_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "#endif\n";

/* ---- store_size_expr ---- */
static const char *const golden_store_size_expr_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 3);\n"
        "#line 2 \"test.nip\"\n"
        "    nippy_handle h = nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)h;\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_store_size_expr_c = {
    golden_store_size_expr_c_parts, 2
};

static const char golden_store_size_expr_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "#endif\n";

/* ---- generic_handle ---- */
static const char *const golden_generic_handle_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "#line 2 \"test.nip\"\n"
        "int32_t nippy_genfn_0(nippy_handle h)\n"
        "{\n"
        "    (void)h;\n"
        "#line 3 \"test.nip\"\n"
        "    return (*(int32_t *)nippy_handle_pointer(h, sizeof(int32_t)));\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t n = nippy_genfn_0(nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 42 }, sizeof(int32_t), _Alignof(int32_t)));\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n",
        "    nippy_store_init(&nippy_stores[0], 1);\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_generic_handle_c = {
    golden_generic_handle_c_parts, 3
};

static const char golden_generic_handle_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef int32_t (*nippy_fun_0)(nippy_handle);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- generic_alloc ---- */
static const char *const golden_generic_alloc_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "#line 2 \"test.nip\"\n"
        "nippy_handle nippy_genfn_0(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 3 \"test.nip\"\n"
        "    return nippy_store_alloc(&nippy_stores[0], 0,"
        " &(int32_t){ x }, sizeof(int32_t), _Alignof(int32_t));\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 5 \"test.nip\"\n"
        "    int32_t n = (*(int32_t"
        " *)nippy_handle_pointer(nippy_genfn_0(7), sizeof(int32_t)));\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n",
        "    nippy_store_init(&nippy_stores[0], 1);\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_generic_alloc_c = {
    golden_generic_alloc_c_parts, 3
};

static const char golden_generic_alloc_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef nippy_handle (*nippy_fun_0)(int32_t);\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

/* ---- handle_union ---- */
static const char *const golden_handle_union_c_parts[] = {
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define NIPPY_STORE_COUNT 1\n"
        "\n"
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n"
        "\n"
        "static void nippy_panic(const char *message)\n"
        "{\n"
        "    fprintf(stderr, \"%s\\n\", message);\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n"
        "{\n"
        "    size_t remainder;\n"
        "\n"
        "    if (alignment <= 1) {\n"
        "        return offset;\n"
        "    }\n"
        "    remainder = offset % alignment;\n"
        "    if (remainder == 0) {\n"
        "        return offset;\n"
        "    }\n"
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    return offset + (alignment - remainder);\n"
        "}\n"
        "\n"
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n"
        "{\n"
        "    if (initial_bytes == 0) {\n"
        "        initial_bytes = 1;\n"
        "    }\n"
        "    store->data = malloc(initial_bytes);\n"
        "    if (store->data == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->used = 0;\n"
        "    store->capacity = initial_bytes;\n"
        "    store->generation = 1;\n"
        "}\n"
        "\n"
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n"
        "{\n"
        "    size_t capacity;\n"
        "    unsigned char *grown;\n"
        "\n"
        "    if (required <= store->capacity) {\n"
        "        return;\n"
        "    }\n"
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n"
        "    while (capacity < required) {\n"
        "        if (capacity > (size_t)-1 / 2) {\n"
        "            nippy_panic(\"nippy: out of memory\");\n"
        "        }\n"
        "        capacity *= 2;\n"
        "    }\n"
        "    grown = realloc(store->data, capacity);\n"
        "    if (grown == NULL) {\n"
        "        nippy_panic(\"nippy: out of memory\");\n"
        "    }\n"
        "    store->data = grown;\n"
        "    store->capacity = capacity;\n"
        "}\n"
        "\n"
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n"
        "                                      const void *value, size_t size,\n"
        "                                      size_t align)\n"
        "{\n"
        "    nippy_handle handle;\n"
        "    size_t offset;\n"
        "    size_t end;\n"
        "\n"
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    if (size == 0) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n"
        "    if (offset > (size_t)-1 - size) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    end = offset + size;\n"
        "    nippy_store_reserve(store, end);\n"
        "    memcpy(store->data + offset, value, size);\n"
        "    store->used = end;\n"
        "    handle.store = (uint32_t)id;\n"
        "    handle.offset = offset;\n"
        "    handle.generation = store->generation;\n"
        "    return handle;\n"
        "}\n"
        "\n"
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n"
        "                                           size_t size)\n"
        "{\n"
        "    nippy_store *store;\n"
        "\n"
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n"
        "        nippy_panic(\"nippy: invalid store id\");\n"
        "    }\n"
        "    store = &nippy_stores[handle.store];\n"
        "    if (handle.generation != store->generation) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    if (handle.offset > store->used ||\n"
        "        size > store->used - handle.offset) {\n"
        "        nippy_panic(\"nippy: handle is out of range\");\n"
        "    }\n"
        "    return store->data + handle.offset;\n"
        "}\n"
        "\n"
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n"
        "                               size_t size)\n"
        "{\n"
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n"
        "}\n"
        "\n"
        "static void nippy_store_reset(nippy_store *store)\n"
        "{\n"
        "    store->used = 0;\n"
        "    if (store->generation == UINT64_MAX) {\n"
        "        nippy_panic(\"nippy: stale handle\");\n"
        "    }\n"
        "    store->generation++;\n"
        "}\n"
        "\n"
        "static void nippy_store_free(nippy_store *store)\n"
        "{\n"
        "    free(store->data);\n"
        "    store->data = NULL;\n"
        "    store->used = 0;\n"
        "    store->capacity = 0;\n"
        "    store->generation = 0;\n"
        "    (void)nippy_store_reserve;\n"
        "    (void)nippy_store_alloc;\n"
        "    (void)nippy_handle_pointer;\n"
        "    (void)nippy_handle_write;\n"
        "    (void)nippy_store_reset;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    nippy_store_init(&nippy_stores[0], 1);\n"
        "#line 3 \"test.nip\"\n"
        "    nippy_handle h = nippy_store_alloc(&nippy_stores[0],"
        " 0, &(int32_t){ 1 }, sizeof(int32_t), _Alignof(int32_t));\n"
        "    (void)h;\n"
        "#line 4 \"test.nip\"\n"
        "    nippy_sum_3 m = ((nippy_sum_3){ .tag = 1, .value.v1 = h });\n"
        "    (void)m;\n"
        "    nippy_store_free(&nippy_stores[0]);\n"
        "    return 0;\n"
        "}\n"
};
static const GoldenText golden_handle_union_c = {
    golden_handle_union_c_parts, 2
};

static const char golden_handle_union_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        nippy_handle v1;\n"
        "    } value;\n"
        "} nippy_sum_3;\n"
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

    return compile_succeeds(source, golden_struct_c,
                            golden_struct_h, "a type declaration");
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

    return compile_succeeds(source, golden_values_c,
                            golden_values_h, "struct values and inference");
}

TestResult test_function_codegen(void)
{
    const char *source =
        "fun adder(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var sum : Int32 = adder(1, 2)\n";

    return compile_succeeds(source, golden_function_c,
                            golden_function_h, "a function declaration");
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

    return compile_succeeds(source, golden_recursive_c,
                            golden_recursive_h, "a recursive function");
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

    return compile_succeeds(source, golden_logic_c,
                            golden_logic_h, "Bool type and logical operators");
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

    return compile_succeeds(source, golden_bool_field_c,
                            golden_bool_field_h, "a Bool struct field");
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

TestResult test_nil_return_codegen(void)
{
    const char *source =
        "fun nothing(): Nil\n"
        "  return nil\n"
        "end\n";

    return compile_succeeds(source, golden_nil_return_c,
                            golden_nil_return_h, "a Nil return");
}

TestResult test_union_vars_codegen(void)
{
    const char *source =
        "type Something = Int32 | Bool | Nil\n"
        "\n"
        "var a: Something = 3\n"
        "var b: Something = true\n"
        "var c: Something = nil\n";

    return compile_succeeds(source, golden_union_vars_c,
                            golden_union_vars_h, "union injection");
}

TestResult test_union_canonical_codegen(void)
{
    const char *source =
        "var a: Bool | Int32 = 3\n"
        "var b: Int32 | Bool = false\n";

    return compile_succeeds(source, golden_union_canonical_c,
                            golden_union_canonical_h, "canonical union order");
}

TestResult test_is_narrow_codegen(void)
{
    const char *source =
        "fun classify(x: Int32 | Bool | Nil): Int32\n"
        "  if typeof x is Int32 then\n"
        "    return x\n"
        "  elseif typeof x is Bool then\n"
        "    if x then\n"
        "      return 1\n"
        "    else\n"
        "      return 0\n"
        "    end\n"
        "  else\n"
        "    return 0\n"
        "  end\n"
        "end\n"
        "\n"
        "var n: Int32 = classify(3)\n";

    return compile_succeeds(source, golden_is_narrow_c,
                            golden_is_narrow_h, "is-test and narrowing");
}

TestResult test_generic_alias_codegen(void)
{
    const char *source =
        "type Option<T> = T | Nil\n"
        "\n"
        "var a: Option<Int32> = 3\n"
        "var b: Option<Int32> = nil\n";

    return compile_succeeds(source, golden_generic_alias_c,
                            golden_generic_alias_h, "a generic alias");
}

TestResult test_generic_struct_codegen(void)
{
    const char *source =
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var p = Pair(1, true)\n";

    return compile_succeeds(source, golden_generic_struct_c,
                            golden_generic_struct_h, "a generic struct");
}

TestResult test_generic_function_codegen(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var n: Int32 = identity(3)\n";

    return compile_succeeds(source, golden_generic_function_c,
                            golden_generic_function_h, "a generic function");
}

TestResult test_generic_two_types_codegen(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var a: Int32 = identity(1)\n"
        "var b: Bool = identity(true)\n";

    return compile_succeeds(source, golden_generic_two_types_c,
                            golden_generic_two_types_h, "two generic instantiations");
}

TestResult test_generic_hof_codegen(void)
{
    const char *source =
        "fun apply<T>(f: Fun<(T):T>, x: T): T\n"
        "  return f(x)\n"
        "end\n"
        "\n"
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "\n"
        "var n: Int32 = apply(inc, 1)\n";

    return compile_succeeds(source, golden_generic_hof_c,
                            golden_generic_hof_h, "a generic higher-order function");
}

TestResult test_generic_nested_codegen(void)
{
    const char *source =
        "type Option<T> = T | Nil\n"
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var o: Option<Int32> = 3\n"
        "var p = Pair(o, false)\n";

    return compile_succeeds(source, golden_generic_nested_c,
                            golden_generic_nested_h, "nested generic types");
}

TestResult test_generic_self_codegen(void)
{
    const char *source =
        "fun count<T>(x: T): T\n"
        "  return count(x)\n"
        "end\n"
        "\n"
        "var n: Int32 = count(1)\n";

    return compile_succeeds(source, golden_generic_self_c,
                            golden_generic_self_h, "a recursive generic function");
}

TestResult test_union_field_codegen(void)
{
    const char *source =
        "type Holder\n"
        "  value: Int32 | Nil\n"
        "end\n"
        "\n"
        "var h: Holder = Holder(nil)\n";

    return compile_succeeds(source, golden_union_field_c,
                            golden_union_field_h, "a union struct field");
}

TestResult test_is_static_codegen(void)
{
    const char *source =
        "fun yes(x: Int32): Bool\n"
        "  return typeof x is Int32\n"
        "end\n"
        "\n"
        "fun no(x: Int32): Bool\n"
        "  return typeof x is Bool\n"
        "end\n";

    return compile_succeeds(source, golden_is_static_c,
                            golden_is_static_h, "statically decided is-tests");
}

TestResult test_is_nil_codegen(void)
{
    const char *source =
        "fun is_nil(x: Int32 | Nil): Bool\n"
        "  if typeof x is Nil then\n"
        "    return true\n"
        "  end\n"
        "  return false\n"
        "end\n";

    return compile_succeeds(source, golden_is_nil_c,
                            golden_is_nil_h, "an is-Nil test");
}

TestResult test_union_struct_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "fun pick(p: Point | Nil): Point\n"
        "  if typeof p is Point then\n"
        "    return p\n"
        "  end\n"
        "  return Point(0, 0)\n"
        "end\n"
        "\n"
        "var q: Point = pick(Point(1, 2))\n";

    return compile_succeeds(source, golden_union_struct_c,
                            golden_union_struct_h, "a struct union alternative");
}

TestResult test_generic_struct_two_codegen(void)
{
    const char *source =
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var a = Pair(1, true)\n"
        "var b = Pair(true, 1)\n";

    return compile_succeeds(source, golden_generic_struct_two_c,
                            golden_generic_struct_two_h,
                            "two generic struct instantiations");
}

TestResult test_generic_struct_arg_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var p = identity(Point(1, 2))\n";

    return compile_succeeds(source, golden_generic_struct_arg_c,
                            golden_generic_struct_arg_h,
                            "a generic function over a struct");
}

TestResult test_generic_two_params_codegen(void)
{
    const char *source =
        "fun first<A, B>(a: A, b: B): A\n"
        "  return a\n"
        "end\n"
        "\n"
        "var n: Int32 = first(1, true)\n";

    return compile_succeeds(source, golden_generic_two_params_c,
                            golden_generic_two_params_h,
                            "a two-parameter generic function");
}

TestResult test_generic_nested_call_codegen(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun wrap<T>(x: T): T\n"
        "  return identity(x)\n"
        "end\n"
        "\n"
        "var n: Int32 = wrap(3)\n";

    return compile_succeeds(source, golden_generic_nested_call_c,
                            golden_generic_nested_call_h,
                            "a generic function calling a generic function");
}

TestResult test_inject_call_codegen(void)
{
    const char *source =
        "fun take(v: Int32 | Nil): Int32\n"
        "  if typeof v is Int32 then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "\n"
        "fun pass(x: Int32): Int32\n"
        "  return take(x)\n"
        "end\n"
        "\n"
        "var n: Int32 = pass(3)\n";

    return compile_succeeds(source, golden_inject_call_c,
                            golden_inject_call_h, "union injection at a call argument");
}

TestResult test_union_field_int_codegen(void)
{
    const char *source =
        "type Holder\n"
        "  value: Int32 | Nil\n"
        "end\n"
        "\n"
        "var h: Holder = Holder(3)\n";

    return compile_succeeds(source, golden_union_field_int_c,
                            golden_union_field_int_h,
                            "union injection into a struct field");
}

TestResult test_generic_struct_named_codegen(void)
{
    const char *source =
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var p = Pair(first = 1, second = true)\n";

    return compile_succeeds(source, golden_generic_struct_named_c,
                            golden_generic_struct_named_h,
                            "a named-argument generic constructor");
}

TestResult test_generic_struct_point_codegen(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n"
        "\n"
        "type Pair<A, B>\n"
        "  first: A\n"
        "  second: B\n"
        "end\n"
        "\n"
        "var p = Pair(Point(1, 2), true)\n";

    return compile_succeeds(source, golden_generic_struct_point_c,
                            golden_generic_struct_point_h,
                            "a generic struct over a struct");
}

TestResult test_alias_chain_codegen(void)
{
    const char *source =
        "type MaybeInt = Int32 | Nil\n"
        "type Value = MaybeInt | Bool\n"
        "\n"
        "var v: Value = true\n"
        "var n: Value = 3\n";

    return compile_succeeds(source, golden_alias_chain_c, golden_alias_chain_h,
                            "a chain of union aliases");
}

TestResult test_generic_union_return_codegen(void)
{
    const char *source =
        "fun maybe<T>(x: T): T | Nil\n"
        "  return x\n"
        "end\n"
        "\n"
        "var m: Int32 | Nil = maybe(3)\n";

    return compile_succeeds(source, golden_generic_union_return_c,
                            golden_generic_union_return_h,
                            "a generic union return");
}

TestResult test_nil_field_param_codegen(void)
{
    const char *source =
        "type Marker\n"
        "  value: Nil\n"
        "end\n"
        "\n"
        "fun take(n: Nil): Int32\n"
        "  return 0\n"
        "end\n"
        "\n"
        "var m: Marker = Marker(nil)\n"
        "var n: Int32 = take(nil)\n";

    return compile_succeeds(source, golden_nil_field_param_c, golden_nil_field_param_h,
                            "Nil in a field and a parameter");
}

TestResult test_store_alloc_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var n: Int32 = p.read()\n";

    return compile_succeeds_parts(source, &golden_store_alloc_c, golden_store_alloc_h,
                            "a store allocation and read");
}

TestResult test_store_write_reset_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var w: Nil = p.write(2)\n"
        "var r: Nil = s.reset()\n";

    return compile_succeeds_parts(source, &golden_store_write_reset_c,
                            golden_store_write_reset_h,
                            "store write and reset");
}

TestResult test_byte_basics_codegen(void)
{
    const char *source =
        "var b: Byte = 255\n"
        "var c: Byte | Nil = 7\n"
        "var ok: Bool = b == 255\n";

    return compile_succeeds(source, golden_byte_basics_c, golden_byte_basics_h,
                            "Byte values and equality");
}

TestResult test_byte_struct_codegen(void)
{
    const char *source =
        "type B\n"
        "  value: Byte\n"
        "end\n"
        "\n"
        "var b: B = B(200)\n";

    return compile_succeeds(source, golden_byte_struct_c, golden_byte_struct_h,
                            "a Byte struct field");
}

TestResult test_handle_field_codegen(void)
{
    const char *source =
        "type Holder\n"
        "  h: Handle<Int32>\n"
        "end\n"
        "\n"
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var holder: Holder = Holder(p)\n";

    return compile_succeeds_parts(source, &golden_handle_field_c, golden_handle_field_h,
                            "a Handle struct field");
}

TestResult test_store_functions_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "\n"
        "fun make(x: Int32): Handle<Int32>\n"
        "  return s.alloc<Int32>(x)\n"
        "end\n"
        "\n"
        "fun read_it(h: Handle<Int32>): Int32\n"
        "  return h.read()\n"
        "end\n"
        "\n"
        "var n: Int32 = read_it(make(42))\n";

    return compile_succeeds_parts(source, &golden_store_functions_c,
                            golden_store_functions_h,
                            "store use inside functions");
}

TestResult test_expression_statement_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "\n"
        "fun clear(): Nil\n"
        "  s.reset()\n"
        "  return nil\n"
        "end\n";

    return compile_succeeds_parts(source, &golden_expression_statement_c,
                            golden_expression_statement_h,
                            "a call expression statement");
}

TestResult test_top_level_reset_codegen(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "s.reset()\n";

    return compile_succeeds_parts(source, &golden_top_level_reset_c,
                            golden_top_level_reset_h,
                            "a top-level call statement");
}

TestResult test_byte_equality_codegen(void)
{
    const char *source =
        "fun is_seven(b: Byte): Bool\n"
        "  return b == 7\n"
        "end\n"
        "\n"
        "var ok: Bool = is_seven(7)\n";

    return compile_succeeds(source, golden_byte_equality_c, golden_byte_equality_h,
                            "Byte equality with a literal");
}

TestResult test_alloc_byte_codegen(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "var b: Handle<Byte> = s.alloc<Byte>(255)\n";

    return compile_succeeds_parts(source, &golden_alloc_byte_c, golden_alloc_byte_h,
                            "allocating a Byte");
}


TestResult test_two_stores_codegen(void)
{
    const char *source =
        "var a: Store = Store(1)\n"
        "var b: Store = Store(1)\n"
        "var p: Handle<Int32> = a.alloc<Int32>(1)\n"
        "var q: Handle<Int32> = b.alloc<Int32>(2)\n"
        "var n: Int32 = p.read() + q.read()\n"
        "a.reset()\n";

    return compile_succeeds_parts(source, &golden_two_stores_c, golden_two_stores_h,
                            "two independent stores");
}

TestResult test_store_size_expr_codegen(void)
{
    const char *source =
        "var s: Store = Store(1 + 2)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n";

    return compile_succeeds_parts(source, &golden_store_size_expr_c,
                            golden_store_size_expr_h,
                            "a constant store size expression");
}

TestResult test_generic_handle_codegen(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun read_it<T>(h: Handle<T>): T\n"
        "  return h.read()\n"
        "end\n"
        "var n: Int32 = read_it(s.alloc<Int32>(42))\n";

    return compile_succeeds_parts(source, &golden_generic_handle_c,
                                  golden_generic_handle_h,
                                  "inference through Handle<T>");
}

TestResult test_generic_alloc_codegen(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun alloc_one<T>(x: T): Handle<T>\n"
        "  return s.alloc<T>(x)\n"
        "end\n"
        "var n: Int32 = alloc_one(7).read()\n";

    return compile_succeeds_parts(source, &golden_generic_alloc_c,
                                  golden_generic_alloc_h,
                                  "allocating a generic value");
}

TestResult test_handle_union_codegen(void)
{
    const char *source =
        "type MaybeHandle = Handle<Int32> | Nil\n"
        "var s: Store = Store(1)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var m: MaybeHandle = h\n";

    return compile_succeeds_parts(source, &golden_handle_union_c, golden_handle_union_h,
                            "a Handle union alternative");
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
    return compile_fails(")\n", "a token that cannot start a declaration",
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

TestResult test_checker_expression_statement(void)
{
    return compile_fails("fun f(x:Int32):Int32 x end\n",
                         "a non-call expression statement",
                         MESSAGE_CHECKER_EXPRESSION_STATEMENT);
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
    TypedType struct_type = {0};
    size_t field_index = 0;
    size_t decl_index = 0;
    size_t struct_type_index = 0;
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
    struct_type.kind = NIPPY_TYPE_STRUCT;
    struct_type.name_index = name;
    struct_type.declaration_node = decl_index;
    struct_type.source_name_index = name;
    typed_program_add_type(&program, &struct_type, &struct_type_index);

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

/* ---- type-system messages ---- */

TestResult test_checker_generic_arity(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "\n"
        "var b: Box = Box(1)\n";

    return compile_fails(source, "a generic type without arguments",
                         MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, "Box");
}

TestResult test_checker_type_not_generic(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "end\n"
        "\n"
        "var p: Point<Int32> = Point(1)\n";

    return compile_fails(source, "arguments on a non-generic type",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Point");
}

TestResult test_checker_type_argument_count(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "\n"
        "var b: Box<Int32, Bool> = Box(1)\n";

    return compile_fails(source, "a generic type with too many arguments",
                         MESSAGE_CHECKER_TYPE_ARGUMENT_COUNT, "Box",
                         (size_t)1, (size_t)2);
}

TestResult test_checker_duplicate_type_parameter(void)
{
    const char *source =
        "type Pair<T, T>\n"
        "  first: T\n"
        "  second: T\n"
        "end\n";

    return compile_fails(source, "a duplicate type parameter",
                         MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER, "T");
}

TestResult test_checker_cannot_infer_type(void)
{
    const char *source =
        "fun make<T>(): T\n"
        "  return nil\n"
        "end\n"
        "\n"
        "var x: Int32 = make()\n";

    return compile_fails(source, "a call with no type to infer",
                         MESSAGE_CHECKER_CANNOT_INFER_TYPE, "T", "make");
}

TestResult test_checker_generic_as_value(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "var f = identity\n";

    return compile_fails(source, "a generic function used as a value",
                         MESSAGE_CHECKER_GENERIC_AS_VALUE, "identity");
}

TestResult test_checker_is_type_is_union(void)
{
    const char *source =
        "fun f(x: Int32 | Bool): Int32\n"
        "  if typeof x is Int32 | Bool then\n"
        "    return 0\n"
        "  end\n"
        "  return 1\n"
        "end\n";

    return compile_fails(source, "an is-test whose target is a union",
                         MESSAGE_CHECKER_IS_TYPE_IS_UNION);
}

TestResult test_checker_recursive_alias(void)
{
    const char *source =
        "type Loop = Loop | Int32\n"
        "var x: Loop = 1\n";

    return compile_fails(source, "a recursive alias",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Loop");
}

TestResult test_checker_nil_type_mismatch(void)
{
    return compile_fails("var x: Int32 = nil\n",
                         "nil initialising an Int32",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "x",
                         "Int32");
}


TestResult test_analyser_invalid_type_child(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    size_t type_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    type.kind = NIPPY_TYPE_UNION;
    type.name_index = TYPED_NO_NODE;
    type.signature_index = TYPED_NO_NODE;
    type.first_child = 0;
    type.child_count = 2;
    if (!typed_program_add_type(&program, &type, &type_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_TYPE_CHILD, type_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid type child did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_declaration(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType type = {0};
    size_t type_index = 0;
    size_t name;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    name = typed_program_copy_symbol(&program, "Point", 5);
    type.kind = NIPPY_TYPE_STRUCT;
    type.name_index = name;
    type.source_name_index = name;
    type.declaration_node = 999;
    if (!typed_program_add_type(&program, &type, &type_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_DECLARATION, type_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid declaration link did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_auxiliary_type(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType bool_type = {0};
    TypedNode node = {0};
    size_t bool_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    bool_type.kind = NIPPY_TYPE_BOOL;
    bool_type.name_index = TYPED_NO_NODE;
    bool_type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &bool_type, &bool_index)) {
        return TEST_ERROR;
    }
    node.kind = TYPED_IS;
    node.type_index = bool_index;
    node.auxiliary_type = 999;
    node.name_index = TYPED_NO_NODE;
    node.left_child = TYPED_NO_NODE;
    node.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &node, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_AUXILIARY_TYPE, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid auxiliary type did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}


/* ---- new parser diagnostics ---- */

TestResult test_parser_expected_type_parameter_name(void)
{
    return compile_fails("fun f<>(x:Int32):Int32\n  return x\nend\n",
                         "an empty type parameter list",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_NAME);
}

TestResult test_parser_expected_type_parameter_separator(void)
{
    return compile_fails("fun f<T U>(x:Int32):Int32\n  return x\nend\n",
                         "type parameters without a separator",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_SEPARATOR);
}

TestResult test_parser_expected_type_parameter_gt(void)
{
    return compile_fails("fun f<T", "a type parameter list without '>'",
                         MESSAGE_PARSER_EXPECTED_TYPE_PARAMETER_GT);
}

TestResult test_parser_expected_type_argument(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<> = 1\n";

    return compile_fails(source, "an empty type argument list",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT);
}

TestResult test_parser_expected_type_argument_separator(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<Int32 Bool> = 1\n";

    return compile_fails(source, "type arguments without a separator",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_SEPARATOR);
}

TestResult test_parser_expected_type_argument_gt(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: T\n"
        "end\n"
        "var b: Box<Int32";

    return compile_fails(source, "a type argument list without '>'",
                         MESSAGE_PARSER_EXPECTED_TYPE_ARGUMENT_GT);
}

TestResult test_parser_expected_union_alternative(void)
{
    return compile_fails("var x: Int32 | = 3\n",
                         "a union without its second alternative",
                         MESSAGE_PARSER_EXPECTED_UNION_ALTERNATIVE);
}

TestResult test_parser_expected_member_name(void)
{
    return compile_fails("fun f(x:Int32):Bool\n  return x. end\nend\n",
                         "a dot without a member name",
                         MESSAGE_PARSER_EXPECTED_MEMBER_NAME);
}

TestResult test_checker_redefines_nil(void)
{
    return compile_fails("type Nil x: Int32 end\n",
                         "a type declaration named Nil",
                         MESSAGE_CHECKER_REDEFINES_NIL);
}


TestResult test_checker_function_field(void)
{
    const char *source =
        "type Holder\n"
        "  f: Fun<(Int32):Int32>\n"
        "end\n";

    return compile_fails(source, "a function-typed struct field",
                         MESSAGE_CHECKER_FIELD_TYPE, "f");
}

TestResult test_checker_recursive_struct(void)
{
    return compile_fails("type Box\n  value: Box\nend\n",
                         "a recursive struct",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Box");
}

TestResult test_checker_recursive_generic_struct(void)
{
    const char *source =
        "type Box<T>\n"
        "  value: Box<T>\n"
        "end\n"
        "var b: Box<Int32> = Box(1)\n";

    return compile_fails(source, "a recursive generic struct",
                         MESSAGE_CHECKER_RECURSIVE_TYPE, "Box");
}

TestResult test_checker_union_injection_mismatch(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "end\n"
        "type S = Int32 | Bool\n"
        "var s: S = Point(1)\n";

    return compile_fails(source, "an incompatible union initializer",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "s", "S");
}


TestResult test_checker_redefines_nil_variable(void)
{
    return compile_fails("var Nil = 1\n", "a variable named Nil",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "Nil");
}

TestResult test_checker_nil_as_value(void)
{
    return compile_fails("var x = Nil\n", "the Nil type used as a value",
                         MESSAGE_CHECKER_TYPE_AS_VALUE, "Nil");
}


TestResult test_checker_construct_alias_union(void)
{
    const char *source =
        "type S = Int32 | Bool\n"
        "var s = S(1)\n";

    return compile_fails(source, "constructing a union alias",
                         MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "S");
}

TestResult test_checker_builtin_type_arguments(void)
{
    return compile_fails("var x: Int32<Bool> = 1\n",
                         "type arguments on a builtin",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Int32");
}

TestResult test_checker_store_size_constant(void)
{
    const char *source =
        "var n: Int32 = 4\n"
        "var s: Store = Store(n)\n";

    return compile_fails(source, "a non-constant store size",
                         MESSAGE_CHECKER_STORE_SIZE);
}

TestResult test_checker_store_initializer(void)
{
    return compile_fails("var s: Store = 16\n",
                         "a store without Store(bytes)",
                         MESSAGE_CHECKER_STORE_INITIALIZER, "s");
}

TestResult test_checker_store_without_annotation(void)
{
    return compile_fails("var s = Store(16)\n",
                         "Store(...) without a Store annotation",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TestResult test_checker_store_as_value(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var t = s\n";

    return compile_fails(source, "a store used as a value",
                         MESSAGE_CHECKER_STORE_NOT_VALUE, "s");
}

TestResult test_checker_duplicate_store(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var s: Store = Store(16)\n";

    return compile_fails(source, "a duplicate store",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "s");
}

TestResult test_checker_store_parameter(void)
{
    return compile_fails("fun f(x: Store): Int32\n  return 0\nend\n",
                         "a Store parameter",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TestResult test_checker_store_return(void)
{
    return compile_fails("fun f(): Store\n  return 0\nend\n",
                         "a Store return type",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TestResult test_checker_store_field(void)
{
    return compile_fails("type H\n  value: Store\nend\n",
                         "a Store struct field",
                         MESSAGE_CHECKER_FIELD_TYPE, "value");
}

TestResult test_checker_store_union(void)
{
    return compile_fails("type S = Store | Nil\n",
                         "a Store union alternative",
                         MESSAGE_CHECKER_UNION_FUNCTION);
}

TestResult test_checker_store_type_arguments(void)
{
    return compile_fails("var s: Store<Int32> = Store(16)\n",
                         "type arguments on Store",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Store");
}

TestResult test_checker_alloc_type_argument(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc(1)\n";

    return compile_fails(source, "alloc without a type argument",
                         MESSAGE_CHECKER_ALLOC_TYPE_ARG);
}

TestResult test_checker_handle_args(void)
{
    return compile_fails(
        "var h: Handle<Int32, Bool> = 0\n",
        "Handle type argument count",
        MESSAGE_CHECKER_HANDLE_ELEMENT);
}

TestResult test_checker_alloc_store(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Store>(nil)\n";

    return compile_fails(source, "alloc of a Store",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_alloc_count(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Int32>()\n";

    return compile_fails(source, "alloc with no value",
                         MESSAGE_CHECKER_ALLOC_ARG_COUNT);
}

TestResult test_checker_alloc_value_type(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Int32>(true)\n";

    return compile_fails(source, "alloc with the wrong value type",
                         MESSAGE_CHECKER_ALLOC_ARG_TYPE, "Int32");
}

TestResult test_checker_handle_element(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var h: Handle<Store> = s.alloc<Int32>(1)\n";

    return compile_fails(source, "a Store handle element",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_unknown_store(void)
{
    return compile_fails("var p = nope.alloc<Int32>(1)\n",
                         "an unknown store",
                         MESSAGE_CHECKER_UNKNOWN_STORE, "nope");
}

TestResult test_checker_reset_args(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var r: Nil = s.reset(1)\n";

    return compile_fails(source, "reset with an argument",
                         MESSAGE_CHECKER_MEMBER_ARGS, "reset");
}

TestResult test_checker_reset_type_argument(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var r: Nil = s.reset<Int32>()\n";

    return compile_fails(source, "reset with a type argument",
                         MESSAGE_CHECKER_MEMBER_TYPE_ARG, "reset");
}

TestResult test_checker_read_on_non_handle(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "var y: Int32 = x.read()\n";

    return compile_fails(source, "read on an Int32",
                         MESSAGE_CHECKER_MEMBER_RECEIVER, "read");
}

TestResult test_checker_write_type(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var w: Nil = p.write(true)\n";

    return compile_fails(source, "write with the wrong value type",
                         MESSAGE_CHECKER_WRITE_TYPE);
}

TestResult test_checker_byte_range(void)
{
    return compile_fails("var b: Byte = 300\n",
                         "a byte literal out of range",
                         MESSAGE_CHECKER_BYTE_RANGE, 300);
}

TestResult test_checker_unknown_member(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.foo()\n";

    return compile_fails(source, "an unknown member",
                         MESSAGE_CHECKER_UNKNOWN_MEMBER, "foo");
}

TestResult test_generator_store_runtime_messages(void)
{
    CompilerConfig config = {.header_name = "test.h",
                             .source_name = "test.nip"};
    CompilerResult result = compiler_compile(
        "var s: Store = Store(16)\nvar p: Handle<Int32> = s.alloc<Int32>(1)\n",
        &config);
    int ok;

    if (!result.succeeded) {
        compiler_result_free(&result);
        return TEST_FAIL;
    }
    ok = result.c_source != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_OUT_OF_MEMORY) != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_STALE_HANDLE) != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_HANDLE_BOUNDS) != NULL &&
         strstr(result.c_source, MESSAGE_RUNTIME_STORE_RANGE) != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  generated store runtime lacks a diagnostic\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TestResult test_analyser_invalid_store_id(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType store_type = {0};
    TypedNode declaration = {0};
    size_t type_index = 0;
    size_t node_index = 0;
    size_t name;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    store_type.kind = NIPPY_TYPE_STORE;
    store_type.name_index = TYPED_NO_NODE;
    store_type.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &store_type, &type_index)) {
        return TEST_ERROR;
    }
    name = typed_program_copy_symbol(&program, "s", 1);
    declaration.kind = TYPED_STORE_DECLARATION;
    declaration.type_index = type_index;
    declaration.store_id = 1;
    declaration.integer_value = 16;
    declaration.name_index = name;
    declaration.left_child = TYPED_NO_NODE;
    declaration.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &declaration, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_STORE_ID, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid store id did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_handle_type(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType handle = {0};
    size_t type_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    handle.kind = NIPPY_TYPE_HANDLE;
    handle.name_index = TYPED_NO_NODE;
    handle.signature_index = TYPED_NO_NODE;
    handle.first_child = 0;
    handle.child_count = 0;
    if (!typed_program_add_type(&program, &handle, &type_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_HANDLE_TYPE, type_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid handle type did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_lexer_unterminated_string(void)
{
    return compile_fails(
        "var s: String<4> = \"ab",
        "an unterminated string literal",
        MESSAGE_LEXER_UNTERMINATED_STRING);
}

TestResult test_lexer_string_newline(void)
{
    return compile_fails(
        "var s: String<8> = \"a\nb\"\n",
        "a newline inside a string literal",
        MESSAGE_LEXER_STRING_NEWLINE);
}

TestResult test_lexer_invalid_escape(void)
{
    return compile_fails(
        "var s: String<8> = \"a\\qb\"\n",
        "an invalid string escape",
        MESSAGE_LEXER_INVALID_ESCAPE, 'q');
}

TestResult test_parser_string_length(void)
{
    return compile_fails(
        "var s: String<> = \"\"\n",
        "a missing Text capacity",
        MESSAGE_PARSER_EXPECTED_STRING_LENGTH);
}

TestResult test_parser_string_gt(void)
{
    return compile_fails(
        "var s: String<4, 8> = \"\"\n",
        "a Text capacity trailing comma",
        MESSAGE_PARSER_EXPECTED_STRING_GT);
}

TestResult test_checker_dynamic_string(void)
{
    return compile_fails(
        "var s: Text = \"x\"\nvar f: String<8> = s\n",
        "a dynamic Text assigned to a fixed Text",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "f", "String<8>");
}

TestResult test_checker_string_too_long(void)
{
    return compile_fails(
        "var s: String<4> = \"hello\"\n",
        "a string literal larger than its capacity",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}

TestResult test_checker_string_literal_context(void)
{
    return compile_fails(
        "var s = \"x\"\n",
        "a string literal without a String<N> context",
        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}

TestResult test_checker_string_compare_context(void)
{
    return compile_fails(
        "var ok: Bool = \"a\" == \"b\"\n",
        "comparing two uncontextual string literals",
        MESSAGE_CHECKER_STRING_COMPARE_CONTEXT);
}

TestResult test_checker_string_ambiguous(void)
{
    return compile_fails(
        "var u: String<4> | String<8> = \"hi\"\n",
        "a literal that fits two union alternatives",
        MESSAGE_CHECKER_STRING_AMBIGUOUS);
}

TestResult test_checker_string_capacity(void)
{
    return compile_fails(
        "var s: String<4097> = \"\"\n",
        "a Text capacity above the limit",
        MESSAGE_CHECKER_STRING_CAPACITY, 4096);
}

TestResult test_checker_string_assignment(void)
{
    return compile_fails(
        "var a: String<8> = \"hi\"\nvar b: String<16> = a\n",
        "assigning a String<8> to a String<16>",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "b", "String<16>");
}

TestResult test_checker_string_equality(void)
{
    return compile_fails(
        "var a: String<8> = \"hi\"\nvar b: String<16> = \"hi\"\nvar ok: Bool "
        "= a == b\n",
        "comparing different Text capacities",
        MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}

TestResult test_checker_string_len_args(void)
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar n: Int32 = s.byte_len(1)\n",
        "byte_len with an argument",
        MESSAGE_CHECKER_MEMBER_ARGS, "byte_len");
}

TestResult test_checker_string_index_type(void)
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar b: Byte = s.byte_at(\"x\")\n",
        "byte_at with a string index",
        MESSAGE_CHECKER_STRING_INDEX);
}

TestResult test_checker_string_method_type_arg(void)
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar n: Int32 = s.byte_len<String<8>>()\n",
        "a type argument on byte_len",
        MESSAGE_CHECKER_MEMBER_TYPE_ARG, "byte_len");
}

TestResult test_checker_string_receiver(void)
{
    return compile_fails(
        "var n: Int32 = 1.byte_len()\n",
        "byte_len on an Int32",
        MESSAGE_CHECKER_MEMBER_RECEIVER, "byte_len");
}

TestResult test_checker_string_literal_receiver(void)
{
    return compile_fails(
        "var n: Int32 = \"hi\".byte_len()\n",
        "byte_len on an uncontextual literal",
        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}

TestResult test_checker_error_args(void)
{
    return compile_fails(
        "var e: Error = Error(\"E\", \"d\")\n",
        "Error with too few arguments",
        MESSAGE_CHECKER_ERROR_ARGS);
}

TestResult test_checker_error_arg_type(void)
{
    return compile_fails(
        "var e: Error = Error(\"E\", 1, \"f\", 1, 2, 3)\n",
        "Error with a non-string description",
        MESSAGE_CHECKER_ERROR_ARG_TYPE, (size_t)2);
}

TestResult test_checker_error_named_args(void)
{
    return compile_fails(
        "var e: Error = Error(header=\"E\", description=\"d\", "
        "filename=\"f\", line=1, col=2, pos=3)\n",
        "Error with named arguments",
        MESSAGE_CHECKER_ERROR_ARGS);
}

TestResult test_checker_error_member(void)
{
    return compile_fails(
        "var n: Int32 = 1.line()\n",
        "an Error field method on an Int32",
        MESSAGE_CHECKER_MEMBER_RECEIVER, "line");
}

TestResult test_checker_redefines_string(void)
{
    return compile_fails(
        "type String = Int32\n",
        "redefining String",
        MESSAGE_CHECKER_REDEFINES_STRING);
}

TestResult test_checker_redefines_text(void)
{
    return compile_fails(
        "type Text = Int32\n",
        "redefining Text",
        MESSAGE_CHECKER_REDEFINES_TEXT);
}

TestResult test_checker_redefines_error(void)
{
    return compile_fails(
        "type Error = Int32\n",
        "redefining Error",
        MESSAGE_CHECKER_REDEFINES_ERROR);
}

TestResult test_checker_string_variable_name(void)
{
    return compile_fails(
        "var Text: Int32 = 1\n",
        "a variable named Text",
        MESSAGE_CHECKER_DUPLICATE_NAME, "Text");
}

TestResult test_analyser_invalid_string_literal(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType string = {0};
    TypedNode literal = {0};
    size_t type_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    string.kind = NIPPY_TYPE_STRING;
    string.name_index = TYPED_NO_NODE;
    string.signature_index = TYPED_NO_NODE;
    string.fixed_length = 4;
    if (!typed_program_add_type(&program, &string, &type_index)) {
        return TEST_ERROR;
    }
    literal.kind = TYPED_STRING_LITERAL;
    literal.type_index = type_index;
    literal.name_index = TYPED_NO_NODE;
    literal.auxiliary_type = TYPED_NO_NODE;
    literal.store_id = TYPED_NO_NODE;
    literal.string_offset = 0;
    literal.string_length = 4;
    literal.left_child = TYPED_NO_NODE;
    literal.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &literal, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_STRING_LITERAL);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid string literal did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

TestResult test_analyser_invalid_error(void)
{
    TypedProgram program;
    ErrorState errors;
    TypedType error = {0};
    TypedNode field = {0};
    size_t type_index = 0;
    size_t node_index = 0;
    char expected[256];

    typed_program_init(&program);
    error_init(&errors);
    error.kind = NIPPY_TYPE_ERROR;
    error.name_index = TYPED_NO_NODE;
    error.signature_index = TYPED_NO_NODE;
    if (!typed_program_add_type(&program, &error, &type_index)) {
        return TEST_ERROR;
    }
    field.kind = TYPED_ERROR_FIELD;
    field.type_index = type_index;
    field.name_index = TYPED_NO_NODE;
    field.auxiliary_type = TYPED_NO_NODE;
    field.store_id = TYPED_NO_NODE;
    field.integer_value = 9;
    field.left_child = TYPED_NO_NODE;
    field.right_child = TYPED_NO_NODE;
    if (!typed_program_push(&program, &field, &node_index) ||
        !list_push(&program.roots, &node_index)) {
        return TEST_ERROR;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_ANALYSER_INVALID_ERROR, node_index);
    if (analyser_analyse(&program, &errors) ||
        !error_contains(&errors, expected)) {
        fprintf(stderr, "  invalid Error node did not produce its diagnostic\n");
        typed_program_free(&program);
        return TEST_FAIL;
    }
    typed_program_free(&program);
    return TEST_PASS;
}

/* Every test in this suite, in run order. The table lives here so adding
   a test needs only its definition above and one entry below. */
static const char golden_string_basic_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "static void nippy_string_bounds(void)\n"
        "{\n"
        "    fprintf(stderr, \"nippy: string index out of bounds\\n\");\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static uint8_t nippy_string_8_at(nippy_string_8 s, int32_t i)\n"
        "{\n"
        "    if (i < 0 || (uint32_t)i >= s.length) {\n"
        "        nippy_string_bounds();\n"
        "    }\n"
        "    return s.data[i];\n"
        "}\n"
        "\n"
        "static bool nippy_string_8_equal(nippy_string_8 a, nippy_string_8 b)\n"
        "{\n"
        "    return a.length == b.length &&\n"
        "           memcmp(a.data, b.data, a.length) == 0;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    nippy_string_8 s = (nippy_string_8){ { 104, 105, 10, 0 }, 3 };\n"
        "    (void)s;\n"
        "#line 2 \"test.nip\"\n"
        "    int32_t n = ((int32_t)(s).length);\n"
        "    (void)n;\n"
        "#line 3 \"test.nip\"\n"
        "    bool ok = (nippy_string_8_equal(s, (nippy_string_8){ { 104, "
        "105, 10, 0 }, 3 }));\n"
        "    (void)ok;\n"
        "    return 0;\n"
        "}\n";

static const char golden_string_basic_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint8_t data[9];\n"
        "    uint32_t length;\n"
        "} nippy_string_8;\n"
        "\n"
        "static uint8_t nippy_string_8_at(nippy_string_8 s, int32_t i);\n"
        "\n"
        "#endif\n";

TestResult test_string_basic_codegen(void)
{
    const char *source =
        "var s: String<8> = \"hi\\n\"\n"
        "var n: Int32 = s.byte_len()\n"
        "var ok: Bool = s == \"hi\\n\"\n"
    ;

    return compile_succeeds(source, golden_string_basic_c,
                            golden_string_basic_h, "string basic");
}


static const char golden_string_union_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "static void nippy_string_bounds(void)\n"
        "{\n"
        "    fprintf(stderr, \"nippy: string index out of bounds\\n\");\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static uint8_t nippy_string_8_at(nippy_string_8 s, int32_t i)\n"
        "{\n"
        "    if (i < 0 || (uint32_t)i >= s.length) {\n"
        "        nippy_string_bounds();\n"
        "    }\n"
        "    return s.data[i];\n"
        "}\n"
        "\n"
        "static bool nippy_string_8_equal(nippy_string_8 a, nippy_string_8 b)\n"
        "{\n"
        "    return a.length == b.length &&\n"
        "           memcmp(a.data, b.data, a.length) == 0;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    nippy_sum_2 u = ((nippy_sum_2){ .tag = 1, .value.v1 = "
        "(nippy_string_8){ { 104, 105, 0 }, 2 } });\n"
        "    (void)u;\n"
        "#line 2 \"test.nip\"\n"
        "    bool ok = (u.tag == 1);\n"
        "    (void)ok;\n"
        "    return 0;\n"
        "}\n";

static const char golden_string_union_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint8_t data[9];\n"
        "    uint32_t length;\n"
        "} nippy_string_8;\n"
        "\n"
        "static uint8_t nippy_string_8_at(nippy_string_8 s, int32_t i);\n"
        "\n"
        "typedef uint8_t nippy_nil;\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t tag;\n"
        "    union {\n"
        "        nippy_string_8 v1;\n"
        "    } value;\n"
        "} nippy_sum_2;\n"
        "\n"
        "#endif\n";

TestResult test_string_union_codegen(void)
{
    const char *source =
        "var u: String<8> | Nil = \"hi\"\n"
        "var ok: Bool = typeof u is String<8>\n"
    ;

    return compile_succeeds(source, golden_string_union_c,
                            golden_string_union_h, "string union");
}


static const char golden_string_error_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "static void nippy_string_bounds(void)\n"
        "{\n"
        "    fprintf(stderr, \"nippy: string index out of bounds\\n\");\n"
        "    exit(1);\n"
        "}\n"
        "\n"
        "static uint8_t nippy_string_32_at(nippy_string_32 s, int32_t i)\n"
        "{\n"
        "    if (i < 0 || (uint32_t)i >= s.length) {\n"
        "        nippy_string_bounds();\n"
        "    }\n"
        "    return s.data[i];\n"
        "}\n"
        "\n"
        "static bool nippy_string_32_equal(nippy_string_32 a, nippy_string_32 b)\n"
        "{\n"
        "    return a.length == b.length &&\n"
        "           memcmp(a.data, b.data, a.length) == 0;\n"
        "}\n"
        "\n"
        "static uint8_t nippy_string_128_at(nippy_string_128 s, int32_t i)\n"
        "{\n"
        "    if (i < 0 || (uint32_t)i >= s.length) {\n"
        "        nippy_string_bounds();\n"
        "    }\n"
        "    return s.data[i];\n"
        "}\n"
        "\n"
        "static bool nippy_string_128_equal(nippy_string_128 a, nippy_string_128 b)\n"
        "{\n"
        "    return a.length == b.length &&\n"
        "           memcmp(a.data, b.data, a.length) == 0;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    nippy_error e = (nippy_error){ (nippy_string_32){ { 69, 0 }, 1 "
        "}, (nippy_string_128){ { 100, 101, 115, 99, 0 }, 4 }, "
        "(nippy_string_128){ { 116, 101, 115, 116, 46, 110, 105, 112, 0 }, "
        "8 }, 1, 2, 3 };\n"
        "    (void)e;\n"
        "#line 2 \"test.nip\"\n"
        "    int32_t l = (e).line;\n"
        "    (void)l;\n"
        "#line 3 \"test.nip\"\n"
        "    nippy_string_32 h = (e).header;\n"
        "    (void)h;\n"
        "    return 0;\n"
        "}\n";

static const char golden_string_error_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint8_t data[33];\n"
        "    uint32_t length;\n"
        "} nippy_string_32;\n"
        "\n"
        "static uint8_t nippy_string_32_at(nippy_string_32 s, int32_t i);\n"
        "\n"
        "typedef struct {\n"
        "    uint8_t data[129];\n"
        "    uint32_t length;\n"
        "} nippy_string_128;\n"
        "\n"
        "static uint8_t nippy_string_128_at(nippy_string_128 s, int32_t i);\n"
        "\n"
        "typedef struct {\n"
        "    nippy_string_32 header;\n"
        "    nippy_string_128 description;\n"
        "    nippy_string_128 filename;\n"
        "    int32_t line;\n"
        "    int32_t col;\n"
        "    int32_t pos;\n"
        "} nippy_error;\n"
        "\n"
        "#endif\n";

TestResult test_string_error_codegen(void)
{
    const char *source =
        "var e: Error = Error(\"E\", \"desc\", \"test.nip\", 1, 2, 3)\n"
        "var l: Int32 = e.line()\n"
        "var h: String<32> = e.header()\n"
    ;

    return compile_succeeds(source, golden_string_error_c,
                            golden_string_error_h, "string error");
}


static const char *const golden_string_store_c_parts[] = {
        "#include \"test.h\"\n",
        "\n",
        "#include <stdio.h>\n",
        "#include <stdlib.h>\n",
        "#include <string.h>\n",
        "\n",
        "#define NIPPY_STORE_COUNT 1\n",
        "\n",
        "static nippy_store nippy_stores[NIPPY_STORE_COUNT];\n",
        "\n",
        "static void nippy_panic(const char *message)\n",
        "{\n",
        "    fprintf(stderr, \"%s\\n\", message);\n",
        "    exit(1);\n",
        "}\n",
        "\n",
        "static size_t nippy_store_align(size_t offset, size_t alignment)\n",
        "{\n",
        "    size_t remainder;\n",
        "\n",
        "    if (alignment <= 1) {\n",
        "        return offset;\n",
        "    }\n",
        "    remainder = offset % alignment;\n",
        "    if (remainder == 0) {\n",
        "        return offset;\n",
        "    }\n",
        "    if (offset > (size_t)-1 - (alignment - remainder)) {\n",
        "        nippy_panic(\"nippy: out of memory\");\n",
        "    }\n",
        "    return offset + (alignment - remainder);\n",
        "}\n",
        "\n",
        "static void nippy_store_init(nippy_store *store, size_t initial_bytes)\n",
        "{\n",
        "    if (initial_bytes == 0) {\n",
        "        initial_bytes = 1;\n",
        "    }\n",
        "    store->data = malloc(initial_bytes);\n",
        "    if (store->data == NULL) {\n",
        "        nippy_panic(\"nippy: out of memory\");\n",
        "    }\n",
        "    store->used = 0;\n",
        "    store->capacity = initial_bytes;\n",
        "    store->generation = 1;\n",
        "}\n",
        "\n",
        "static void nippy_store_reserve(nippy_store *store, size_t required)\n",
        "{\n",
        "    size_t capacity;\n",
        "    unsigned char *grown;\n",
        "\n",
        "    if (required <= store->capacity) {\n",
        "        return;\n",
        "    }\n",
        "    capacity = store->capacity == 0 ? 1 : store->capacity;\n",
        "    while (capacity < required) {\n",
        "        if (capacity > (size_t)-1 / 2) {\n",
        "            nippy_panic(\"nippy: out of memory\");\n",
        "        }\n",
        "        capacity *= 2;\n",
        "    }\n",
        "    grown = realloc(store->data, capacity);\n",
        "    if (grown == NULL) {\n",
        "        nippy_panic(\"nippy: out of memory\");\n",
        "    }\n",
        "    store->data = grown;\n",
        "    store->capacity = capacity;\n",
        "}\n",
        "\n",
        "static nippy_handle nippy_store_alloc(nippy_store *store, size_t id,\n",
        "                                      const void *value, size_t size,\n",
        "                                      size_t align)\n",
        "{\n",
        "    nippy_handle handle;\n",
        "    size_t offset;\n",
        "    size_t end;\n",
        "\n",
        "    if (id >= (size_t)NIPPY_STORE_COUNT) {\n",
        "        nippy_panic(\"nippy: invalid store id\");\n",
        "    }\n",
        "    if (size == 0) {\n",
        "        nippy_panic(\"nippy: handle is out of range\");\n",
        "    }\n",
        "    offset = nippy_store_align(store->used, align);\n",
        "    if (offset > (size_t)-1 - size) {\n",
        "        nippy_panic(\"nippy: handle is out of range\");\n",
        "    }\n",
        "    end = offset + size;\n",
        "    nippy_store_reserve(store, end);\n",
        "    memcpy(store->data + offset, value, size);\n",
        "    store->used = end;\n",
        "    handle.store = (uint32_t)id;\n",
        "    handle.offset = offset;\n",
        "    handle.generation = store->generation;\n",
        "    return handle;\n",
        "}\n",
        "\n",
        "static unsigned char *nippy_handle_pointer(nippy_handle handle,\n",
        "                                           size_t size)\n",
        "{\n",
        "    nippy_store *store;\n",
        "\n",
        "    if ((size_t)handle.store >= (size_t)NIPPY_STORE_COUNT) {\n",
        "        nippy_panic(\"nippy: invalid store id\");\n",
        "    }\n",
        "    store = &nippy_stores[handle.store];\n",
        "    if (handle.generation != store->generation) {\n",
        "        nippy_panic(\"nippy: stale handle\");\n",
        "    }\n",
        "    if (handle.offset > store->used ||\n",
        "        size > store->used - handle.offset) {\n",
        "        nippy_panic(\"nippy: handle is out of range\");\n",
        "    }\n",
        "    return store->data + handle.offset;\n",
        "}\n",
        "\n",
        "static void nippy_handle_write(nippy_handle handle, const void *value,\n",
        "                               size_t size)\n",
        "{\n",
        "    memcpy(nippy_handle_pointer(handle, size), value, size);\n",
        "}\n",
        "\n",
        "static void nippy_store_reset(nippy_store *store)\n",
        "{\n",
        "    store->used = 0;\n",
        "    if (store->generation == UINT64_MAX) {\n",
        "        nippy_panic(\"nippy: stale handle\");\n",
        "    }\n",
        "    store->generation++;\n",
        "}\n",
        "\n",
        "static void nippy_store_free(nippy_store *store)\n",
        "{\n",
        "    free(store->data);\n",
        "    store->data = NULL;\n",
        "    store->used = 0;\n",
        "    store->capacity = 0;\n",
        "    store->generation = 0;\n",
        "    (void)nippy_store_reserve;\n",
        "    (void)nippy_store_alloc;\n",
        "    (void)nippy_handle_pointer;\n",
        "    (void)nippy_handle_write;\n",
        "    (void)nippy_store_reset;\n",
        "}\n",
        "\n",
        "static void nippy_string_bounds(void)\n",
        "{\n",
        "    fprintf(stderr, \"nippy: string index out of bounds\\n\");\n",
        "    exit(1);\n",
        "}\n",
        "\n",
        "static uint8_t nippy_string_8_at(nippy_string_8 s, int32_t i)\n",
        "{\n",
        "    if (i < 0 || (uint32_t)i >= s.length) {\n",
        "        nippy_string_bounds();\n",
        "    }\n",
        "    return s.data[i];\n",
        "}\n",
        "\n",
        "static bool nippy_string_8_equal(nippy_string_8 a, nippy_string_8 b)\n",
        "{\n",
        "    return a.length == b.length &&\n",
        "           memcmp(a.data, b.data, a.length) == 0;\n",
        "}\n",
        "\n",
        "int main(void)\n",
        "{\n",
        "    nippy_store_init(&nippy_stores[0], 8);\n",
        "#line 2 \"test.nip\"\n",
        "    nippy_handle h = nippy_store_alloc(&nippy_stores[0], 0, ",
        "&(union { nippy_string_8 nippy_value; }){ .nippy_value = ",
        "(nippy_string_8){ { 104, 105, 0 }, 2 } }, sizeof(nippy_string_8), ",
        "_Alignof(nippy_string_8));\n",
        "    (void)h;\n",
        "#line 3 \"test.nip\"\n",
        "    nippy_string_8 s = (*(nippy_string_8 *)nippy_handle_pointer(h, ",
        "sizeof(nippy_string_8)));\n",
        "    (void)s;\n",
        "    nippy_store_free(&nippy_stores[0]);\n",
        "    return 0;\n",
        "}\n",
};
static const GoldenText golden_string_store_c = { golden_string_store_c_parts, 182 };

static const char golden_string_store_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "\n"
        "typedef struct {\n"
        "    uint32_t store;\n"
        "    size_t offset;\n"
        "    uint64_t generation;\n"
        "} nippy_handle;\n"
        "\n"
        "typedef struct {\n"
        "    unsigned char *data;\n"
        "    size_t used;\n"
        "    size_t capacity;\n"
        "    uint64_t generation;\n"
        "} nippy_store;\n"
        "\n"
        "typedef struct {\n"
        "    uint8_t data[9];\n"
        "    uint32_t length;\n"
        "} nippy_string_8;\n"
        "\n"
        "static uint8_t nippy_string_8_at(nippy_string_8 s, int32_t i);\n"
        "\n"
        "#endif\n";

TestResult test_string_store_codegen(void)
{
    const char *source =
        "var arena: Store = Store(8)\n"
        "var h: Handle<String<8>> = arena.alloc<String<8>>(\"hi\")\n"
        "var s: String<8> = h.read()\n"
    ;

    return compile_succeeds_parts(source, &golden_string_store_c,
                                  golden_string_store_h,
                                  "string store");
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
         strstr(result.h_source, "nippy_string_8 header;") != NULL &&
         strstr(result.h_source, "nippy_string_16 description;") != NULL &&
         strstr(result.h_source, "nippy_string_24 filename;") != NULL;
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
         strstr(result.h_source, "nippy_string_8") != NULL;
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
         strstr(result.h_source, "nippy_string_8192") != NULL;
    compiler_result_free(&result);
    if (!ok) {
        fprintf(stderr, "  raised maximum string capacity was not used\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static const char golden_assignment_top_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    int32_t x = 1;\n"
        "#line 2 \"test.nip\"\n"
        "    x = 2;\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_assignment_top_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "int32_t nippy_program_value(void);\n"
        "\n"
        "#endif\n";

TestResult test_assignment_top_codegen(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n"
    ;

    return compile_succeeds(source, golden_assignment_top_c,
                            golden_assignment_top_h, "assignment top");
}


static const char golden_assignment_param_c[] =
        "#include \"test.h\"\n"
        "\n"
        "#include <stdio.h>\n"
        "\n"
        "#line 1 \"test.nip\"\n"
        "int32_t bump(int32_t x)\n"
        "{\n"
        "    (void)x;\n"
        "#line 2 \"test.nip\"\n"
        "    x = (int32_t)((uint32_t)(x) + (uint32_t)(1));\n"
        "#line 3 \"test.nip\"\n"
        "    return x;\n"
        "}\n"
        "\n"
        "int32_t nippy_program_value(void)\n"
        "{\n"
        "#line 6 \"test.nip\"\n"
        "    int32_t n = bump(1);\n"
        "    return n;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)nippy_program_value());\n"
        "    return 0;\n"
        "}\n";

static const char golden_assignment_param_h[] =
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

TestResult test_assignment_param_codegen(void)
{
    const char *source =
        "fun bump(x: Int32): Int32\n"
        "  x = x + 1\n"
        "  return x\n"
        "end\n"
        "\n"
        "var n: Int32 = bump(1)\n"
    ;

    return compile_succeeds(source, golden_assignment_param_c,
                            golden_assignment_param_h, "assignment param");
}


TestResult test_checker_assign_unknown(void)
{
    return compile_fails(
        "x = 1\n",
        "an assignment to an unknown name",
        MESSAGE_CHECKER_UNKNOWN_NAME, "x");
}

TestResult test_checker_assign_store(void)
{
    return compile_fails(
        "var s: Store = Store(8)\ns = Store(8)\n",
        "an assignment to a store",
        MESSAGE_CHECKER_ASSIGN_TARGET, "s");
}

TestResult test_checker_assign_function(void)
{
    return compile_fails(
        "fun f(): Nil\n  return nil\nend\nf = 1\n",
        "an assignment to a function",
        MESSAGE_CHECKER_ASSIGN_TARGET, "f");
}

TestResult test_checker_assign_type(void)
{
    return compile_fails(
        "type P\n  x: Int32\nend\nP = 1\n",
        "an assignment to a type",
        MESSAGE_CHECKER_ASSIGN_TARGET, "P");
}

TestResult test_checker_assign_builtin(void)
{
    return compile_fails(
        "Int32 = 1\n",
        "an assignment to a built-in type",
        MESSAGE_CHECKER_ASSIGN_TARGET, "Int32");
}

TestResult test_checker_assign_global_in_function(void)
{
    return compile_fails(
        "var n: Int32 = 1\nfun f(): Nil\n  n = 2\n  return nil\nend\n",
        "assigning a program-level variable inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "n");
}

TestResult test_checker_assign_type_mismatch(void)
{
    return compile_fails(
        "var n: Int32 = 1\nn = true\n",
        "assigning a Bool to an Int32",
        MESSAGE_CHECKER_ASSIGN_TYPE, "n");
}

TestResult test_checker_assign_byte_range(void)
{
    return compile_fails(
        "var b: Byte = 1\nb = 300\n",
        "assigning an out-of-range Byte literal",
        MESSAGE_CHECKER_BYTE_RANGE, 300);
}

TestResult test_checker_assign_string_too_long(void)
{
    return compile_fails(
        "var s: String<4> = \"a\"\ns = \"hello\"\n",
        "assigning a literal larger than the destination",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}

TestResult test_parser_assign_missing_value(void)
{
    return compile_fails(
        "var n: Int32 = 1\nn =\n",
        "an assignment without a value",
        MESSAGE_PARSER_EXPECTED_EXPRESSION);
}

static const char golden_assignment_main_c[] =
        "#include \"test.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "#line 1 \"test.nip\"\n"
        "    int32_t a = 1;\n"
        "    (void)a;\n"
        "#line 2 \"test.nip\"\n"
        "    int32_t b = 2;\n"
        "    (void)b;\n"
        "#line 3 \"test.nip\"\n"
        "    a = b;\n"
        "    return 0;\n"
        "}\n";

static const char golden_assignment_main_h[] =
        "#ifndef NIPPY_GENERATED_H\n"
        "#define NIPPY_GENERATED_H\n"
        "\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "#endif\n";

TestResult test_assignment_main_codegen(void)
{
    const char *source =
        "var a: Int32 = 1\n"
        "var b: Int32 = 2\n"
        "a = b\n"
    ;

    return compile_succeeds(source, golden_assignment_main_c,
                            golden_assignment_main_h, "main assignment");
}



TestResult test_parser_assignment_expression(void)
{
    return compile_fails(
        "var n: Int32 = 1\nvar m: Int32 = (n = 2)\n",
        "assignment used as an expression",
        MESSAGE_PARSER_EXPECTED_RPAREN);
}


TestResult test_checker_global_declaration(void)
{
    return compile_fails(
        "global x: Int32 = 1\n",
        "a global declaration that is not a Store",
        MESSAGE_CHECKER_GLOBAL_DECLARATION, "x");
}

TestResult test_checker_global_store_initializer(void)
{
    return compile_fails(
        "global arena: Store = 1\n",
        "a global Store with a non-Store initializer",
        MESSAGE_CHECKER_STORE_INITIALIZER, "arena");
}

TestResult test_checker_concat_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var s: Text = arena.concat(f, \"y\")\n",
        "concat with a fixed string",
        MESSAGE_CHECKER_CONCAT_TYPE);
}

TestResult test_checker_copy_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var s: Text = arena.copy(f)\n",
        "copy with a fixed string",
        MESSAGE_CHECKER_COPY_TYPE);
}

TestResult test_checker_store_string_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\nvar s: Text = arena.text(\"hi\")\n",
        "store.text with a dynamic literal",
        MESSAGE_CHECKER_STORE_TEXT_TYPE);
}

TestResult test_checker_slice_type(void)
{
    return compile_fails(
        "var s: Text = \"hello\"\nvar t: Text = s.slice(\"x\", 1)\n",
        "slice with a non-Int32 start",
        MESSAGE_CHECKER_STRING_SLICE_TYPE);
}

TestResult test_checker_dynamic_fixed_equality(void)
{
    return compile_fails(
        "var f: String<8> = \"x\"\nvar s: Text = \"x\"\nvar ok: Bool = f == s\n",
        "comparing fixed and dynamic strings",
        MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}

TestResult test_checker_builder_capacity(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(\"x\")\n",
        "builder with a non-Int32 capacity",
        MESSAGE_CHECKER_BUILDER_CAPACITY);
}

TestResult test_checker_builder_append_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append(f)\n",
        "builder append with a fixed string",
        MESSAGE_CHECKER_BUILDER_APPEND_TYPE);
}

TestResult test_checker_builder_byte_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append_byte(\"x\")\n",
        "builder append_byte with a string",
        MESSAGE_CHECKER_BUILDER_BYTE_TYPE);
}

TestResult test_checker_builder_args(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "var s: Text = b.finish(1)\n",
        "builder finish with an argument",
        MESSAGE_CHECKER_MEMBER_ARGS, "finish");
}

TestResult test_checker_builder_redefinition(void)
{
    return compile_fails(
        "type TextBuilder = Int32\n",
        "redefining TextBuilder",
        MESSAGE_CHECKER_REDEFINES_BUILDER);
}

TestResult test_parser_list_capacity(void)
{
    return compile_fails(
        "var xs: Array<Int32,> = [1]\n",
        "list missing capacity",
        MESSAGE_PARSER_EXPECTED_ARRAY_CAPACITY);
}

TestResult test_parser_list_gt(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1 = [1]\n",
        "list missing >",
        MESSAGE_PARSER_EXPECTED_ARRAY_GT);
}

TestResult test_parser_list_rbracket(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1\n",
        "list missing ]",
        MESSAGE_PARSER_EXPECTED_LIST_RBRACKET);
}

TestResult test_parser_list_separator(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1 2]\n",
        "list missing comma",
        MESSAGE_PARSER_EXPECTED_LIST_SEPARATOR);
}

TestResult test_checker_list_context(void)
{
    return compile_fails(
        "var xs: List<Int32> = [1]\n",
        "dynamic list literal",
        MESSAGE_CHECKER_LIST_LITERAL_CONTEXT);
}

TestResult test_checker_list_inferred_context(void)
{
    return compile_fails(
        "var xs = []\n",
        "empty inferred list literal",
        MESSAGE_CHECKER_LIST_INFER);
}

TestResult test_checker_list_length(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2, 3]\n",
        "fixed list over capacity",
        MESSAGE_CHECKER_LIST_LITERAL_LENGTH, 3, 2);
}

TestResult test_checker_list_element(void)
{
    return compile_fails(
        "var xs: Array<Store, 1> = []\n",
        "list element not sized",
        MESSAGE_CHECKER_ARRAY_ELEMENT);
}

TestResult test_checker_list_args(void)
{
    return compile_fails(
        "var xs: List = []\n",
        "bare List",
        MESSAGE_CHECKER_LIST_ARGS);
}

TestResult test_checker_list_capacity(void)
{
    return compile_fails(
        "var xs: Array<Int32, 4097> = []\n",
        "list capacity too large",
        MESSAGE_CHECKER_ARRAY_CAPACITY, NIPPY_DEFAULT_MAX_LIST_CAPACITY);
}

TestResult test_checker_list_index(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar n: Int32 = xs[\"x\"]\n",
        "list index type",
        MESSAGE_CHECKER_LIST_INDEX);
}

TestResult test_checker_list_value(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(true)\n",
        "list value type",
        MESSAGE_CHECKER_LIST_VALUE);
}

TestResult test_checker_list_create_type_arg(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list(1)\n",
        "list create type argument",
        MESSAGE_CHECKER_LIST_TYPE_ARG);
}

TestResult test_checker_list_create_args(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>()\n",
        "list create argument count",
        MESSAGE_CHECKER_LIST_CREATE_ARGS);
}

TestResult test_checker_list_capacity_arg(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(\"x\")\n",
        "list capacity argument type",
        MESSAGE_CHECKER_LIST_CAPACITY_ARG);
}

TestResult test_checker_list_redefinition(void)
{
    return compile_fails(
        "type List = Int32\n",
        "redefining List",
        MESSAGE_CHECKER_REDEFINES_LIST);
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

TestResult test_parser_index_rbracket(void)
{
    return compile_fails(
        "var n: Int32 = [1, 2][0\n",
        "index missing ]",
        MESSAGE_PARSER_EXPECTED_RBRACKET);
}

TestResult test_checker_list_push_lvalue(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\narena.list<Int32>(1).push(1)\n",
        "push temporary list",
        MESSAGE_CHECKER_LIST_LVALUE);
}

TestResult test_checker_list_index_type(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar n: Int32 = xs[\"x\"]\n",
        "index type",
        MESSAGE_CHECKER_LIST_INDEX);
}

TestResult test_checker_slice_dynamic(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar s: Slice<Int32> = xs.slice(0, 1)\n",
        "slice fixed list",
        MESSAGE_CHECKER_SLICE_DYNAMIC);
}

TestResult test_checker_slice_readonly(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(1)\n"
        "var s: Slice<Int32> = xs.slice(0, 1)\n"
        "s[0] = 2\n",
        "slice index write",
        MESSAGE_CHECKER_SLICE_READONLY);
}

TestResult test_checker_slice_args(void)
{
    return compile_fails(
        "var s: Slice = []\n",
        "bare Slice",
        MESSAGE_CHECKER_SLICE_ARGS);
}

TestResult test_checker_slice_element(void)
{
    return compile_fails(
        "var s: Slice<Store> = nil\n",
        "Slice element type",
        MESSAGE_CHECKER_SLICE_ELEMENT);
}

TestResult test_checker_slice_redefinition(void)
{
    return compile_fails(
        "type Slice = Int32\n",
        "redefining Slice",
        MESSAGE_CHECKER_REDEFINES_SLICE);
}

TestResult test_checker_local_store(void)
{
    return compile_fails(
        "fun f(): Nil\n  var s: Store = Store(1)\n  return nil\nend\n",
        "local Store",
        MESSAGE_CHECKER_LOCAL_STORE);
}

TestResult test_checker_local_scope(void)
{
    return compile_fails(
        "fun f(): Int32\n"
        "  if true then\n"
        "    var y: Int32 = 1\n"
        "  end\n"
        "  return y\n"
        "end\n",
        "local out of scope",
        MESSAGE_CHECKER_UNKNOWN_NAME, "y");
}

TestResult test_checker_local_shadow(void)
{
    return compile_fails(
        "fun f(x: Int32): Int32\n  var x: Int32 = 1\n  return x\nend\n",
        "local shadows parameter",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}

TestResult test_checker_unknown_field(void)
{
    return compile_fails(
        "type P\n  x: Int32\nend\nfun f(p: P): Int32\n  return p.y\nend\n",
        "unknown field",
        MESSAGE_CHECKER_UNKNOWN_FIELD, "y");
}

TestResult test_checker_error_field_write(void)
{
    return compile_fails(
        "fun f(e: Error): Nil\n  e.line = 1\n  return nil\nend\n",
        "Error field write",
        MESSAGE_CHECKER_FIELD_LVALUE);
}

TestResult test_parser_for_in(void)
{
    return compile_fails(
        "for x 1 do\nend\n",
        "for missing in",
        MESSAGE_PARSER_EXPECTED_IN);
}

TestResult test_parser_for_do(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nfor x in xs\nend\n",
        "for missing do",
        MESSAGE_PARSER_EXPECTED_DO);
}

TestResult test_checker_for_iterable(void)
{
    return compile_fails(
        "for x in 1 do\nend\n",
        "for non-iterable",
        MESSAGE_CHECKER_FOR_ITERABLE);
}

TestResult test_checker_for_duplicate_binding(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2]\nfor x, x in xs do\nend\n",
        "for duplicate binding",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}

TestResult test_checker_for_parameter_collision(void)
{
    return compile_fails(
        "fun f(x: Int32, xs: Array<Int32, 2>): Nil\n"
        "  for x in xs do\n"
        "  end\n"
        "  return nil\n"
        "end\n",
        "for parameter collision",
        MESSAGE_CHECKER_DUPLICATE_NAME, "x");
}

TestResult test_checker_handle_collection(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var h: Handle<List<Int32>> = "
        "arena.alloc<List<Int32>>(arena.list<Int32>(1))\n",
        "handle of a collection",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_alloc_text(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var h: Handle<Text> = arena.alloc<Text>(arena.text(f))\n",
        "alloc of a Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_list_of_list(void)
{
    return compile_fails(
        "var xs: List<List<Int32>> = []\n",
        "List of List",
        MESSAGE_CHECKER_COLLECTION_ELEMENT);
}

TestResult test_checker_array_of_array(void)
{
    return compile_fails(
        "var xs: Array<Array<Int32, 1>, 1> = []\n",
        "Array of Array",
        MESSAGE_CHECKER_ARRAY_ELEMENT);
}

TestResult test_checker_bare_string(void)
{
    return compile_fails(
        "var s: String = \"hello\"\n",
        "bare String",
        MESSAGE_CHECKER_STRING_ARGS);
}

TestResult test_parser_dynamic_list_gt(void)
{
    return compile_fails(
        "var xs: List<Int32 = []\n",
        "dynamic list missing >",
        MESSAGE_PARSER_EXPECTED_LIST_GT);
}

TestResult test_parser_list_capacity_split(void)
{
    return compile_fails(
        "var xs: List<Int32, 1> = [1]\n",
        "List capacity",
        MESSAGE_PARSER_LIST_CAPACITY);
}

TestResult test_checker_array_args(void)
{
    return compile_fails(
        "var xs: Array = []\n",
        "bare Array",
        MESSAGE_CHECKER_ARRAY_ARGS);
}

TestResult test_checker_redefines_array(void)
{
    return compile_fails(
        "type Array = Int32\n",
        "redefining Array",
        MESSAGE_CHECKER_REDEFINES_ARRAY);
}

TestResult test_checker_handle_builder(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var h: Handle<TextBuilder> = "
        "arena.alloc<TextBuilder>(arena.builder(1))\n",
        "handle of a builder",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_list_of_slice(void)
{
    return compile_fails(
        "var xs: List<Slice<Int32>> = []\n",
        "List of Slice",
        MESSAGE_CHECKER_COLLECTION_ELEMENT);
}

TestResult test_checker_alloc_struct_with_text(void)
{
    return compile_fails(
        "type S\n"
        "  t: Text\n"
        "end\n"
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var s: S = S(t)\n"
        "var h: Handle<S> = arena.alloc<S>(s)\n",
        "alloc of a struct with Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_alloc_array_with_text(void)
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var a: Array<Text, 1> = [t]\n"
        "var h: Handle<Array<Text, 1>> = "
        "arena.alloc<Array<Text, 1>>(a)\n",
        "alloc of an Array with Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_print_statement(void)
{
    return compile_fails(
        "var n: Nil = print(1)\n",
        "print as a value",
        MESSAGE_CHECKER_PRINT_STATEMENT);
}

TestResult test_checker_print_named(void)
{
    return compile_fails(
        "print(x = 1)\n",
        "print named argument",
        MESSAGE_CHECKER_PRINT_NAMED);
}

TestResult test_checker_print_reserved(void)
{
    return compile_fails(
        "fun print(x: Int32): Nil\n  return nil\nend\n",
        "print reserved",
        MESSAGE_CHECKER_PRINT_RESERVED);
}

TestResult test_checker_error_throw_args(void)
{
    return compile_fails(
        "var e: Error = Error.throw(\"E\")\n",
        "Error.throw argument count",
        MESSAGE_CHECKER_ERROR_THROW_ARGS);
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

TestResult test_checker_record_context(void)
{
    return compile_fails(
        "var x = {\"a\": true}\n",
        "Record literal context",
        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
}

TestResult test_checker_record_duplicate(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 4> = {\"a\": true, \"a\": false}\n",
        "Record duplicate key",
        MESSAGE_CHECKER_RECORD_DUPLICATE_KEY);
}

TestResult test_checker_record_key(void)
{
    return compile_fails(
        "var x: Record<Int32, Bool, 4> = {}\n",
        "Record key type",
        MESSAGE_CHECKER_MAP_KEY);
}

TestResult test_checker_record_value(void)
{
    return compile_fails(
        "var x: Record<String<8>, List<Int32>, 4> = {}\n",
        "Record value collection",
        MESSAGE_CHECKER_MAP_VALUE);
}

TestResult test_checker_record_capacity(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 4097> = {}\n",
        "Record capacity",
        MESSAGE_CHECKER_RECORD_CAPACITY);
}

TestResult test_checker_record_literal_capacity(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 1> = {\"a\": true, \"b\": false}\n",
        "Record literal capacity",
        MESSAGE_CHECKER_RECORD_CAPACITY);
}

TestResult test_checker_handle_record(void)
{
    return compile_fails(
        "var x: Handle<Record<String<8>, Bool, 4>> = 0\n",
        "Handle Record",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_alloc_record(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var x: Handle<Record<String<8>, Bool, 4>> = "
        "arena.alloc<Record<String<8>, Bool, 4>>({})\n",
        "alloc Record",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TestResult test_checker_table_key(void)
{
    return compile_fails(
        "var x: Table<Text, Bool> = arena.table<Text, Bool>(1)\n",
        "Table key type",
        MESSAGE_CHECKER_MAP_KEY);
}

TestResult test_checker_table_literal(void)
{
    return compile_fails(
        "var x: Table<String<8>, Bool> = {}\n",
        "Table literal",
        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
}

TestResult test_checker_record_args(void)
{
    return compile_fails(
        "var x: Record = {}\n",
        "bare Record",
        MESSAGE_CHECKER_RECORD_ARGS);
}

TestResult test_checker_table_args(void)
{
    return compile_fails(
        "var x: Table = arena.table(1)\n",
        "bare Table",
        MESSAGE_CHECKER_TABLE_ARGS);
}

TestResult test_checker_redefines_record(void)
{
    return compile_fails(
        "type Record = Int32\n",
        "redefining Record",
        MESSAGE_CHECKER_REDEFINES_RECORD);
}

TestResult test_checker_redefines_table(void)
{
    return compile_fails(
        "type Table = Int32\n",
        "redefining Table",
        MESSAGE_CHECKER_REDEFINES_TABLE);
}

TestResult test_parser_record_capacity(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool> = {}\n",
        "Record missing capacity",
        MESSAGE_PARSER_EXPECTED_RECORD_CAPACITY);
}

TestResult test_parser_record_gt(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2 = {}\n",
        "Record missing >",
        MESSAGE_PARSER_EXPECTED_RECORD_GT);
}

TestResult test_parser_table_gt(void)
{
    return compile_fails(
        "var x: Table<String<8>, Bool = arena.table<String<8>, Bool>(1)\n",
        "Table missing >",
        MESSAGE_PARSER_EXPECTED_TABLE_GT);
}

TestResult test_parser_record_colon(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2> = {\"a\" true}\n",
        "Record missing colon",
        MESSAGE_PARSER_EXPECTED_COLON);
}

TestResult test_parser_record_rbrace(void)
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 2> = {\"a\": true\n",
        "Record missing }",
        MESSAGE_PARSER_EXPECTED_RBRACE);
}

TestResult test_parser_typeof_missing_is(void)
{
    return compile_fails(
        "var b: Bool = typeof 1\n",
        "typeof missing is",
        MESSAGE_PARSER_EXPECTED_IS);
}

TestResult test_parser_match_head(void)
{
    return compile_fails(
        "match 1 with 1 then\nend\n",
        "match head expression",
        MESSAGE_PARSER_MATCH_HEAD);
}

TestResult test_parser_match_with(void)
{
    return compile_fails(
        "var x: Bool = true\nmatch x 1 then\nend\n",
        "match missing with",
        MESSAGE_PARSER_EXPECTED_WITH);
}

TestResult test_parser_if_expression_else(void)
{
    return compile_fails(
        "var z: Int32 = if true then 1 end\n",
        "if expression missing else",
        MESSAGE_PARSER_EXPECTED_ELSE);
}

TestResult test_checker_if_expression_mismatch(void)
{
    return compile_fails(
        "var z: Int32 = if true then 1 else false end\n",
        "if expression type mismatch",
        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
}

TestResult test_parser_match_expression_else(void)
{
    return compile_fails(
        "var b: Bool = true\n"
        "var z: Int32 = match b with true then 1 end\n",
        "match expression missing else",
        MESSAGE_PARSER_EXPECTED_ELSE);
}

TestResult test_checker_match_expression_mismatch(void)
{
    return compile_fails(
        "var b: Bool = true\n"
        "var z: Int32 = match b with true then 1 else false end\n",
        "match expression type mismatch",
        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
}

TestResult test_checker_const_assign(void)
{
    return compile_fails(
        "const x: Int32 = 1\nx = 2\n",
        "assign to const",
        MESSAGE_CHECKER_CONST_ASSIGN, "x");
}

TestResult test_checker_local_const_assign(void)
{
    return compile_fails(
        "fun f(): Nil\n  const x: Int32 = 1\n  x = 2\n  return nil\nend\n",
        "assign to local const",
        MESSAGE_CHECKER_CONST_ASSIGN, "x");
}

TestResult test_checker_const_store(void)
{
    return compile_fails(
        "const s: Store = Store(16)\n",
        "const Store",
        MESSAGE_CHECKER_CONST_STORE);
}

TestResult test_checker_match_not_exhaustive(void)
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with Int32 then\n"
        "  print(\"i\")\n"
        "end\n",
        "non-exhaustive type match",
        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
}

TestResult test_checker_bool_match_not_exhaustive(void)
{
    return compile_fails(
        "var x: Bool = true\n"
        "match x with true then\n"
        "  print(\"t\")\n"
        "end\n",
        "non-exhaustive bool match",
        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
}

TestResult test_checker_match_duplicate(void)
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with Int32 then\n"
        "  print(\"i\")\n"
        "with Int32 then\n"
        "  print(\"j\")\n"
        "end\n",
        "duplicate match pattern",
        MESSAGE_CHECKER_MATCH_DUPLICATE);
}

TestResult test_checker_match_pattern(void)
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with String<8> then\n"
        "  print(\"s\")\n"
        "else\n"
        "  print(\"o\")\n"
        "end\n",
        "match pattern does not fit",
        MESSAGE_CHECKER_MATCH_PATTERN);
}

TestResult test_checker_literal_divide_zero(void)
{
    return compile_fails(
        "var x: Int32 = 1 / 0\n",
        "literal division by zero",
        MESSAGE_CHECKER_ZERO_DIVISOR, "/");
}

TestResult test_checker_literal_modulo_zero(void)
{
    return compile_fails(
        "var x: Int32 = 1 % 0\n",
        "literal modulo by zero",
        MESSAGE_CHECKER_ZERO_DIVISOR, "%");
}

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
    {"inference through Handle<T>", test_generic_handle_codegen},
    {"allocating a generic value", test_generic_alloc_codegen},
    {"a Handle union alternative", test_handle_union_codegen},
    {"two independent stores", test_two_stores_codegen},
    {"a constant store size expression", test_store_size_expr_codegen},

    {"a store allocation and read", test_store_alloc_codegen},
    {"store write and reset", test_store_write_reset_codegen},
    {"Byte values and equality", test_byte_basics_codegen},
    {"a Byte struct field", test_byte_struct_codegen},
    {"a Handle struct field", test_handle_field_codegen},
    {"store use inside functions", test_store_functions_codegen},
    {"a call expression statement", test_expression_statement_codegen},
    {"a top-level call statement", test_top_level_reset_codegen},
    {"Byte equality with a literal", test_byte_equality_codegen},
    {"allocating a Byte", test_alloc_byte_codegen},
    {"a generic union return", test_generic_union_return_codegen},
    {"Nil in a field and a parameter", test_nil_field_param_codegen},
    {"a named-argument generic constructor", test_generic_struct_named_codegen},
    {"a generic struct over a struct", test_generic_struct_point_codegen},
    {"a chain of union aliases", test_alias_chain_codegen},
    {"a two-parameter generic function", test_generic_two_params_codegen},
    {"a generic function calling a generic function", test_generic_nested_call_codegen},
    {"union injection at a call argument", test_inject_call_codegen},
    {"union injection into a struct field", test_union_field_int_codegen},
    {"statically decided is-tests", test_is_static_codegen},
    {"an is-Nil test", test_is_nil_codegen},
    {"a struct union alternative", test_union_struct_codegen},
    {"two generic struct instantiations", test_generic_struct_two_codegen},
    {"a generic function over a struct", test_generic_struct_arg_codegen},
    {"a Nil return", test_nil_return_codegen},
    {"union injection", test_union_vars_codegen},
    {"canonical union order", test_union_canonical_codegen},
    {"is-test and narrowing", test_is_narrow_codegen},
    {"a generic alias", test_generic_alias_codegen},
    {"a generic struct", test_generic_struct_codegen},
    {"a generic function", test_generic_function_codegen},
    {"two generic instantiations", test_generic_two_types_codegen},
    {"a generic higher-order function", test_generic_hof_codegen},
    {"nested generic types", test_generic_nested_codegen},
    {"a recursive generic function", test_generic_self_codegen},
    {"a union struct field", test_union_field_codegen},
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
    {"checker: expression statement must be a call", test_checker_expression_statement},
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
    {"checker: generic type without arguments", test_checker_generic_arity},
    {"checker: type arguments on non-generic type", test_checker_type_not_generic},
    {"checker: generic type argument count", test_checker_type_argument_count},
    {"checker: duplicate type parameter", test_checker_duplicate_type_parameter},
    {"checker: cannot infer type parameter", test_checker_cannot_infer_type},
    {"checker: generic function as value", test_checker_generic_as_value},
    {"checker: is-test target is a union", test_checker_is_type_is_union},
    {"checker: recursive alias", test_checker_recursive_alias},
    {"checker: nil type mismatch", test_checker_nil_type_mismatch},
    {"analyser: invalid type child", test_analyser_invalid_type_child},
    {"analyser: invalid declaration", test_analyser_invalid_declaration},
    {"analyser: invalid auxiliary type", test_analyser_invalid_auxiliary_type},
    {"parser: type parameter name", test_parser_expected_type_parameter_name},
    {"parser: type parameter separator", test_parser_expected_type_parameter_separator},
    {"parser: type parameter gt", test_parser_expected_type_parameter_gt},
    {"parser: type argument", test_parser_expected_type_argument},
    {"parser: type argument separator", test_parser_expected_type_argument_separator},
    {"parser: type argument gt", test_parser_expected_type_argument_gt},
    {"parser: union alternative", test_parser_expected_union_alternative},
    {"parser: member name", test_parser_expected_member_name},
    {"checker: redefines Nil", test_checker_redefines_nil},
    {"checker: function field", test_checker_function_field},
    {"checker: recursive struct", test_checker_recursive_struct},
    {"checker: recursive generic struct", test_checker_recursive_generic_struct},
    {"checker: incompatible union injection", test_checker_union_injection_mismatch},
    {"checker: variable named Nil", test_checker_redefines_nil_variable},
    {"checker: Nil as a value", test_checker_nil_as_value},
    {"checker: construct union alias", test_checker_construct_alias_union},
    {"checker: builtin type arguments", test_checker_builtin_type_arguments},
    {"store: size must be constant", test_checker_store_size_constant},
    {"store: initializer must be Store(bytes)", test_checker_store_initializer},
    {"store: global declaration", test_checker_global_declaration},
    {"store: global initializer", test_checker_global_store_initializer},
    {"store: missing annotation", test_checker_store_without_annotation},
    {"store: not a value", test_checker_store_as_value},
    {"store: duplicate name", test_checker_duplicate_store},
    {"store: parameter type", test_checker_store_parameter},
    {"store: return type", test_checker_store_return},
    {"store: struct field", test_checker_store_field},
    {"store: union alternative", test_checker_store_union},
    {"store: type arguments", test_checker_store_type_arguments},
    {"store: alloc type argument", test_checker_alloc_type_argument},
    {"store: alloc Store", test_checker_alloc_store},
    {"store: Handle argument count", test_checker_handle_args},
    {"print as a value", test_checker_print_statement},
    {"print named argument", test_checker_print_named},
    {"print reserved", test_checker_print_reserved},
    {"assign to const", test_checker_const_assign},
    {"assign to local const", test_checker_local_const_assign},
    {"const Store", test_checker_const_store},
    {"non-exhaustive type match", test_checker_match_not_exhaustive},
    {"non-exhaustive bool match", test_checker_bool_match_not_exhaustive},
    {"duplicate match pattern", test_checker_match_duplicate},
    {"match pattern does not fit", test_checker_match_pattern},
    {"literal division by zero", test_checker_literal_divide_zero},
    {"literal modulo by zero", test_checker_literal_modulo_zero},
    {"typeof missing is", test_parser_typeof_missing_is},
    {"match head expression", test_parser_match_head},
    {"match missing with", test_parser_match_with},
    {"if expression missing else", test_parser_if_expression_else},
    {"if expression type mismatch", test_checker_if_expression_mismatch},
    {"match expression missing else", test_parser_match_expression_else},
    {"match expression type mismatch", test_checker_match_expression_mismatch},
    {"Record literal context", test_checker_record_context},
    {"Record duplicate key", test_checker_record_duplicate},
    {"Record missing colon", test_parser_record_colon},
    {"Record missing }", test_parser_record_rbrace},
    {"Record key type", test_checker_record_key},
    {"Record value collection", test_checker_record_value},
    {"Record capacity", test_checker_record_capacity},
    {"Record literal capacity", test_checker_record_literal_capacity},
    {"Handle Record", test_checker_handle_record},
    {"alloc Record", test_checker_alloc_record},
    {"Table key type", test_checker_table_key},
    {"Table literal", test_checker_table_literal},
    {"bare Record", test_checker_record_args},
    {"bare Table", test_checker_table_args},
    {"redefining Record", test_checker_redefines_record},
    {"redefining Table", test_checker_redefines_table},
    {"Record missing capacity", test_parser_record_capacity},
    {"Record missing >", test_parser_record_gt},
    {"Table missing >", test_parser_table_gt},
    {"Error.throw argument count", test_checker_error_throw_args},
    {"store: alloc argument count", test_checker_alloc_count},
    {"store: alloc value type", test_checker_alloc_value_type},
    {"store: handle element", test_checker_handle_element},
    {"store: unknown store", test_checker_unknown_store},
    {"store: reset arguments", test_checker_reset_args},
    {"store: reset type argument", test_checker_reset_type_argument},
    {"store: read on non-handle", test_checker_read_on_non_handle},
    {"store: write type", test_checker_write_type},
    {"byte: literal range", test_checker_byte_range},
    {"store: unknown member", test_checker_unknown_member},
    {"store: runtime diagnostics emitted", test_generator_store_runtime_messages},
    {"analyser: invalid store id", test_analyser_invalid_store_id},
    {"analyser: invalid handle type", test_analyser_invalid_handle_type},
    {"string: an unterminated string literal", test_lexer_unterminated_string},
    {"string: a newline inside a string literal", test_lexer_string_newline},
    {"string: an invalid string escape", test_lexer_invalid_escape},
    {"string: a missing Text capacity", test_parser_string_length},
    {"string: a Text capacity trailing comma", test_parser_string_gt},
    {"string: dynamic to fixed conversion", test_checker_dynamic_string},
    {"string: concat with a fixed string", test_checker_concat_type},
    {"string: copy with a fixed string", test_checker_copy_type},
    {"string: store.string with a dynamic literal",
     test_checker_store_string_type},
    {"string: slice with a non-Int32 start", test_checker_slice_type},
    {"string: comparing fixed and dynamic strings",
     test_checker_dynamic_fixed_equality},
    {"builder: builder with a non-Int32 capacity", test_checker_builder_capacity},
    {"builder: builder append with a fixed string", test_checker_builder_append_type},
    {"builder: builder append_byte with a string", test_checker_builder_byte_type},
    {"builder: builder finish with an argument", test_checker_builder_args},
    {"builder: redefining TextBuilder", test_checker_builder_redefinition},
    {"list: list missing capacity", test_parser_list_capacity},
    {"list: list missing >", test_parser_list_gt},
    {"list: dynamic list missing >", test_parser_dynamic_list_gt},
    {"list: list missing ]", test_parser_list_rbracket},
    {"list: list missing comma", test_parser_list_separator},
    {"list: dynamic list literal", test_checker_list_context},
    {"list: empty inferred literal", test_checker_list_inferred_context},
    {"list: fixed list over capacity", test_checker_list_length},
    {"list: list element not sized", test_checker_list_element},
    {"list: bare List", test_checker_list_args},
    {"list: list capacity too large", test_checker_list_capacity},
    {"list: index missing ]", test_parser_index_rbracket},
    {"list: push temporary list", test_checker_list_push_lvalue},
    {"list: index type", test_checker_list_index_type},
    {"for missing in", test_parser_for_in},
    {"for missing do", test_parser_for_do},
    {"for non-iterable", test_checker_for_iterable},
    {"for duplicate binding", test_checker_for_duplicate_binding},
    {"for parameter collision", test_checker_for_parameter_collision},
    {"slice: slice fixed list", test_checker_slice_dynamic},
    {"slice: slice index write", test_checker_slice_readonly},
    {"slice: bare Slice", test_checker_slice_args},
    {"slice: Slice element type", test_checker_slice_element},
    {"slice: redefining Slice", test_checker_slice_redefinition},
    {"list: list index type", test_checker_list_index},
    {"handle of a collection", test_checker_handle_collection},
    {"alloc of a Text", test_checker_alloc_text},
    {"alloc of a struct with Text", test_checker_alloc_struct_with_text},
    {"alloc of an Array with Text", test_checker_alloc_array_with_text},
    {"List of List", test_checker_list_of_list},
    {"handle of a builder", test_checker_handle_builder},
    {"List of Slice", test_checker_list_of_slice},
    {"Array of Array", test_checker_array_of_array},
    {"bare String", test_checker_bare_string},
    {"List capacity", test_parser_list_capacity_split},
    {"bare Array", test_checker_array_args},
    {"redefining Array", test_checker_redefines_array},
    {"local Store", test_checker_local_store},
    {"local out of scope", test_checker_local_scope},
    {"local shadows parameter", test_checker_local_shadow},
    {"unknown field", test_checker_unknown_field},
    {"Error field write", test_checker_error_field_write},
    {"list: list value type", test_checker_list_value},
    {"list: list create type argument", test_checker_list_create_type_arg},
    {"list: list create argument count", test_checker_list_create_args},
    {"list: list capacity argument type", test_checker_list_capacity_arg},
    {"list: redefining List", test_checker_list_redefinition},
    {"list: custom maximum capacity", test_compiler_max_list_capacity},
    {"print: custom maximum depth", test_compiler_print_depth},
    {"print: invalid maximum depth", test_compiler_invalid_print_depth},
    {"list: configured maximum exceeded", test_compiler_max_list_exceeded},
    {"list: invalid maximum capacity", test_compiler_invalid_max_list},
    {"string: a string literal larger than its capacity", test_checker_string_too_long},
    {"string: a string literal without a String<N> context",
     test_checker_string_literal_context},
    {"string: comparing two uncontextual string literals",
     test_checker_string_compare_context},
    {"string: a literal that fits two union alternatives",
     test_checker_string_ambiguous},
    {"string: a Text capacity above the limit", test_checker_string_capacity},
    {"string: assigning a String<8> to a String<16>", test_checker_string_assignment},
    {"string: comparing different Text capacities", test_checker_string_equality},
    {"string: byte_len with an argument", test_checker_string_len_args},
    {"string: byte_at with a string index", test_checker_string_index_type},
    {"string: a type argument on byte_len", test_checker_string_method_type_arg},
    {"string: byte_len on an Int32", test_checker_string_receiver},
    {"string: byte_len on an uncontextual literal",
     test_checker_string_literal_receiver},
    {"string: Error with too few arguments", test_checker_error_args},
    {"string: Error with a non-string description", test_checker_error_arg_type},
    {"string: Error with named arguments", test_checker_error_named_args},
    {"string: an Error field method on an Int32", test_checker_error_member},
    {"string: redefining String", test_checker_redefines_string},
    {"string: redefining Text", test_checker_redefines_text},
    {"string: redefining Error", test_checker_redefines_error},
    {"string: a variable named Text", test_checker_string_variable_name},
    {"analyser: invalid string literal", test_analyser_invalid_string_literal},
    {"analyser: invalid Error shape", test_analyser_invalid_error},
    {"string: string basic codegen", test_string_basic_codegen},
    {"string: string union codegen", test_string_union_codegen},
    {"string: string error codegen", test_string_error_codegen},
    {"string: string store codegen", test_string_store_codegen},
    {"compiler: custom Error capacities", test_compiler_error_capacities},
    {"compiler: invalid Error capacity", test_compiler_error_capacity_invalid},
    {"compiler: custom maximum string capacity", test_compiler_max_string_capacity},
    {"compiler: maximum string capacity exceeded", test_compiler_max_string_exceeded},
    {"compiler: invalid maximum string capacity", test_compiler_max_string_invalid},
    {"compiler: raised maximum string capacity", test_compiler_max_string_raised},
    {"compiler: configured source limit", test_compiler_source_limit},
    {"assignment: top-level print", test_assignment_top_codegen},
    {"assignment: parameter", test_assignment_param_codegen},
    {"assignment: main", test_assignment_main_codegen},
    {"assignment: an assignment to an unknown name", test_checker_assign_unknown},
    {"assignment: an assignment to a store", test_checker_assign_store},
    {"assignment: an assignment to a function", test_checker_assign_function},
    {"assignment: an assignment to a type", test_checker_assign_type},
    {"assignment: an assignment to a built-in type", test_checker_assign_builtin},
    {"assignment: assigning a program-level variable inside a function",
     test_checker_assign_global_in_function},
    {"assignment: assigning a Bool to an Int32",
     test_checker_assign_type_mismatch},
    {"assignment: assigning an out-of-range Byte literal",
     test_checker_assign_byte_range},
    {"assignment: assigning a literal larger than the destination",
     test_checker_assign_string_too_long},
    {"assignment: an assignment without a value", test_parser_assign_missing_value},
    {"assignment: assignment as an expression", test_parser_assignment_expression},
};

int main(void)
{
    return test_run_suite("compiler", tests,
                          sizeof(tests) / sizeof(tests[0]));
}
