#include "test_cases.h"

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
#include <sys/wait.h>
#include <unistd.h>

static const char *const temporary_files[] = {
    "tests/.test-empty.nip",
    "tests/.test-empty-bin",
    "tests/.test-empty-bin.c",
    "tests/.test-empty-bin.h",
    "tests/.test-empty.log",
    "tests/.test-13.nip",
    "tests/.test-13-bin",
    "tests/.test-13-bin.c",
    "tests/.test-13-bin.h",
    "tests/.test-compile.log",
    "tests/.test-run.log",
    "tests/.test-struct.nip",
    "tests/.test-struct-bin",
    "tests/.test-struct-bin.c",
    "tests/.test-struct-bin.h",
    "tests/.test-struct.log",
    "tests/.test-values.nip",
    "tests/.test-values-bin",
    "tests/.test-values-bin.c",
    "tests/.test-values-bin.h",
    "tests/.test-values.log",
    "tests/.test-fun.nip",
    "tests/.test-fun-bin",
    "tests/.test-fun-bin.c",
    "tests/.test-fun-bin.h",
    "tests/.test-fun.log",
    "tests/.test.nip",
    "tests/.test-bin",
    "tests/.test-bin.c",
    "tests/.test-bin.h",
    "tests/.test.log"
};

static void cleanup(void)
{
    size_t i;
    for (i = 0; i < sizeof(temporary_files) / sizeof(temporary_files[0]); i++) {
        remove(temporary_files[i]);
    }
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int ok;

    if (file == NULL) {
        perror(path);
        return 0;
    }
    ok = fputs(text, file) >= 0;
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

static int write_bytes(const char *path, const unsigned char *data, size_t length)
{
    FILE *file = fopen(path, "wb");
    int ok;

    if (file == NULL) {
        perror(path);
        return 0;
    }
    ok = fwrite(data, 1, length, file) == length;
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

static int file_has_content(const char *path)
{
    FILE *file = fopen(path, "r");
    int first;

    if (file == NULL) {
        return 0;
    }
    first = fgetc(file);
    fclose(file);
    return first != EOF;
}

static int file_contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "r");
    char buffer[512];
    int found = 0;

    if (file == NULL) {
        return 0;
    }
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        if (strstr(buffer, needle) != NULL) {
            found = 1;
            break;
        }
    }
    fclose(file);
    return found;
}

static int compile_source(const char *source, const char *source_path,
                          const char *binary, const char *log)
{
    char command[512];

    if (!write_text(source_path, source)) {
        return -1;
    }
    snprintf(command, sizeof(command), "./bin/nippy %s %s > %s 2>&1",
             source_path, binary, log);
    return system(command);
}

static int run_compiler(const char *source_path, const char *binary,
                        const char *log)
{
    char command[512];

    snprintf(command, sizeof(command), "./bin/nippy %s %s > %s 2>&1",
             source_path, binary, log);
    return system(command);
}

static int run_command(const char *command, const char *log)
{
    char buffer[512];

    snprintf(buffer, sizeof(buffer), "%s > %s 2>&1", command, log);
    return system(buffer);
}

/* Compile `source`, expecting it to be rejected with a diagnostic that
   contains `format` rendered with `...`. Tests pass the MESSAGE_* macro itself
   so the assertion and the message cannot drift apart. */
static TestResult expect_diagnostic(const char *source, const char *what,
                                    const char *format, ...)
{
    const char *source_path = "tests/.test.nip";
    const char *binary = "tests/.test-bin";
    const char *log = "tests/.test.log";
    char expected[256];
    va_list args;
    TestResult result = TEST_ERROR;
    int status;

    va_start(args, format);
    vsnprintf(expected, sizeof(expected), format, args);
    va_end(args);

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for %s\n", what);
        goto finished;
    }
    if (status == 0) {
        fprintf(stderr, "  %s unexpectedly compiled\n", what);
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains(log, expected)) {
        fprintf(stderr, "  %s: expected diagnostic containing \"%s\"\n",
                what, expected);
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

static int error_contains(const ErrorState *errors, const char *needle)
{
    return strstr(errors->message, needle) != NULL;
}

static int output_is_13(void)
{
    FILE *file = fopen("tests/.test-run.log", "r");
    char output[16];
    int ok;

    if (file == NULL) {
        return 0;
    }
    ok = fgets(output, sizeof(output), file) != NULL &&
         output[0] == '1' && output[1] == '3' && output[2] == '\n' &&
         output[3] == '\0' && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static int output_is(const char *expected)
{
    FILE *file = fopen("tests/.test-run.log", "r");
    char output[64];
    size_t length = strlen(expected);
    int ok;

    if (file == NULL) {
        return 0;
    }
    ok = fgets(output, sizeof(output), file) != NULL &&
         strncmp(output, expected, length) == 0 &&
         output[length] == '\n' && output[length + 1] == '\0' &&
         fgetc(file) == EOF;
    fclose(file);
    return ok;
}

/* Build the exact text a message macro produces for `...`. */
static void expect_message(char *buffer, size_t size, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, size, format, args);
    va_end(args);
}

/* ---- positive (compile-and-run / codegen) ---- */

TestResult test_13_compiles_and_runs(void)
{
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source("var x : Int32 = 13\n", "tests/.test-13.nip",
                            "tests/.test-13-bin", "tests/.test-compile.log");
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for source 13\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  source 13 did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-13-bin.c",
                       "#line 1 \"tests/.test-13.nip\"")) {
        fprintf(stderr, "  generated C lacks the #line directive\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-13-bin > tests/.test-run.log 2>&1");
    if (status == -1) {
        fprintf(stderr, "  could not launch generated program\n");
        goto finished;
    }
    if (status != 0 || !output_is_13()) {
        fprintf(stderr, "  generated program did not print exactly 13\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_type_declaration_compiles(void)
{
    const char *source =
        "type Point\n"
        "  x: Int32\n"
        "  y: Int32\n"
        "end\n";
    const char *source_path = "tests/.test-struct.nip";
    const char *binary = "tests/.test-struct-bin";
    const char *log = "tests/.test-struct.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for a type declaration\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  type declaration did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-struct-bin.h", "typedef struct {") ||
        !file_contains("tests/.test-struct-bin.h", "    int32_t x;") ||
        !file_contains("tests/.test-struct-bin.h", "    int32_t y;") ||
        !file_contains("tests/.test-struct-bin.h", "} Point;") ||
        !file_contains("tests/.test-struct-bin.h",
                       "#line 1 \"tests/.test-struct.nip\"")) {
        fprintf(stderr, "  generated struct does not match the declaration\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-struct-bin > tests/.test-struct.log 2>&1");
    if (status != 0) {
        fprintf(stderr, "  generated type program did not run successfully\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_struct_values_and_inference(void)
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
    const char *source_path = "tests/.test-values.nip";
    const char *binary = "tests/.test-values-bin";
    const char *log = "tests/.test-values.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for struct values\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  struct values did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-values-bin.c",
                       "    Point p1 = (Point){ 1, 2 };") ||
        !file_contains("tests/.test-values-bin.c",
                       "    Point p2 = (Point){ 3, 4 };") ||
        !file_contains("tests/.test-values-bin.c",
                       "    Point p3 = (Point){ 6, 7 };")) {
        fprintf(stderr, "  generated struct initialisers are wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-values-bin > tests/.test-values.log 2>&1");
    if (status != 0) {
        fprintf(stderr, "  generated struct value program did not run\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_function_compiles_and_runs(void)
{
    const char *source =
        "fun adder(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var sum : Int32 = adder(1, 2)\n";
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for a function\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  function program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.c",
                       "int32_t adder(int32_t x, int32_t y)") ||
        !file_contains("tests/.test-fun-bin.h",
                       "typedef int32_t (*nippy_fun_") ||
        !file_contains("tests/.test-fun-bin.c", "adder(1, 2)")) {
        fprintf(stderr, "  generated function or call is wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("3")) {
        fprintf(stderr, "  function program did not print exactly 3\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_function_type_and_higher_order(void)
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
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for a higher-order function\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  higher-order function program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.c",
                       "int32_t apply(nippy_fun_")) {
        fprintf(stderr, "  function-typed parameter was not generated\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("42")) {
        fprintf(stderr, "  higher-order function program did not print 42\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_function_type_annotation_and_struct_params(void)
{
    const char *source =
        "fun add(x:Int32, y:Int32):Int32\n"
        "  return x + y\n"
        "end\n"
        "\n"
        "var f : Fun<(Int32, Int32):Int32> = add\n"
        "var n : Int32 = f(2, 3)\n";
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for a function value\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  function value program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.c", "f = add;") ||
        !file_contains("tests/.test-fun-bin.c", "int32_t n = f(2, 3);")) {
        fprintf(stderr, "  function value or indirect call is wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0) {
        fprintf(stderr, "  function value program did not run\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_recursive_function_compiles(void)
{
    const char *source =
        "fun countdown(n:Int32):Int32\n"
        "  return countdown(n)\n"
        "end\n"
        "\n"
        "var zero : Int32 = 0\n";
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for a recursive function\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  recursive function did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.c", "return countdown(n);")) {
        fprintf(stderr, "  recursive call was not generated\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("0")) {
        fprintf(stderr, "  recursive function program did not print 0\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_bool_and_if_elseif_else(void)
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
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for if/elseif/else\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  if/elseif/else program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.c", "if (") ||
        !file_contains("tests/.test-fun-bin.c", "} else {") ||
        !file_contains("tests/.test-fun-bin.c", "x == 0")) {
        fprintf(stderr, "  generated if/elseif/else is wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("1")) {
        fprintf(stderr, "  if/elseif/else program did not print 1\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_while_break_continue(void)
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
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for while/break/continue\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  while/break/continue program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.c", "while (true)") ||
        !file_contains("tests/.test-fun-bin.c", "break;") ||
        !file_contains("tests/.test-fun-bin.c", "continue;")) {
        fprintf(stderr, "  generated loop control is wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("7")) {
        fprintf(stderr, "  while/break/continue program did not print 7\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_bool_type_and_logical_operators(void)
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
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for Bool operators\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  Bool operator program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.h", "#include <stdbool.h>") ||
        !file_contains("tests/.test-fun-bin.c", "bool flag") ||
        !file_contains("tests/.test-fun-bin.c", "&&") ||
        !file_contains("tests/.test-fun-bin.c", "||")) {
        fprintf(stderr, "  generated Bool code is wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("1")) {
        fprintf(stderr, "  Bool operator program did not print 1\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_bool_struct_field(void)
{
    const char *source =
        "type Flags\n"
        "  on: Bool\n"
        "  count: Int32\n"
        "end\n"
        "\n"
        "var f : Flags = Flags(true, 3)\n";
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(source, source_path, binary, log);
    if (status == -1) {
        fprintf(stderr, "  could not launch compiler for a Bool field\n");
        goto finished;
    }
    if (status != 0) {
        fprintf(stderr, "  Bool field program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test-fun-bin.h", "    bool on;") ||
        !file_contains("tests/.test-fun-bin.h", "    int32_t count;")) {
        fprintf(stderr, "  generated Bool field is wrong\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0) {
        fprintf(stderr, "  Bool field program did not run\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_program_is_newline_agnostic(void)
{
    const char *single_line =
        "fun f(x:Int32):Int32 if x > 0 then return 1 else return 0 end end "
        "var n:Int32=f(1)\n";
    const char *scattered =
        "fun\tf(x:Int32):Int32\n\n"
        "if x>0\nthen\nreturn\t1\nelse\nreturn 0\nend\nend\n"
        "var\tn\t:\tInt32\t=\tf(\t1\t)\n";
    const char *source_path = "tests/.test-fun.nip";
    const char *binary = "tests/.test-fun-bin";
    const char *log = "tests/.test-fun.log";
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = compile_source(single_line, source_path, binary, log);
    if (status != 0) {
        fprintf(stderr, "  a single-line program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("1")) {
        fprintf(stderr, "  single-line program did not print 1\n");
        result = TEST_FAIL;
        goto finished;
    }

    cleanup();
    status = compile_source(scattered, source_path, binary, log);
    if (status != 0) {
        fprintf(stderr, "  a whitespace-scattered program did not compile\n");
        result = TEST_FAIL;
        goto finished;
    }
    status = system("tests/.test-fun-bin > tests/.test-run.log 2>&1");
    if (status != 0 || !output_is("1")) {
        fprintf(stderr, "  whitespace-scattered program did not print 1\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

/* ---- lexer messages ---- */

TestResult test_lexer_integer_out_of_range(void)
{
    return expect_diagnostic("var x : Int32 = 99999999999999999999999999\n",
                             "an out-of-range integer literal",
                             MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
}

TestResult test_lexer_identifier_too_long(void)
{
    return expect_diagnostic(
        "var abcdefghijklmnopqrstuvwxyzABCDEFG : Int32 = 1\n",
        "an over-long identifier",
        MESSAGE_LEXER_IDENTIFIER_TOO_LONG, NIPPY_IDENTIFIER_MAX - 1);
}

TestResult test_lexer_unexpected_character(void)
{
    return expect_diagnostic("var x : Int32 = 13 @\n",
                             "an unexpected printable character",
                             MESSAGE_LEXER_UNEXPECTED_CHARACTER, '@');
}

TestResult test_lexer_unexpected_byte(void)
{
    return expect_diagnostic("var x : Int32 = 13 \x80\n",
                             "an unexpected non-printable byte",
                             MESSAGE_LEXER_UNEXPECTED_BYTE, 0x80);
}

/* ---- parser messages ---- */

TestResult test_parser_expected_var_type_or_fun(void)
{
    return expect_diagnostic("13\n", "a bare integer literal program",
                             MESSAGE_PARSER_EXPECTED_VAR_TYPE_OR_FUN);
}

TestResult test_parser_expected_variable_name(void)
{
    return expect_diagnostic("var : Int32 = 1\n", "a missing variable name",
                             MESSAGE_PARSER_EXPECTED_VARIABLE_NAME);
}

TestResult test_parser_expected_type_name(void)
{
    return expect_diagnostic("var x : = 1\n", "a missing type annotation",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME);
}

TestResult test_parser_expected_equals(void)
{
    return expect_diagnostic("var x : Int32 13\n", "a declaration without '='",
                             MESSAGE_PARSER_EXPECTED_EQUALS);
}

TestResult test_parser_expected_expression(void)
{
    return expect_diagnostic("var x : Int32 = +\n",
                             "an invalid initialiser expression",
                             MESSAGE_PARSER_EXPECTED_EXPRESSION);
}

TestResult test_parser_expected_type_name_after_type(void)
{
    return expect_diagnostic("type 13 x: Int32 end\n",
                             "a type declaration without a name",
                             MESSAGE_PARSER_EXPECTED_TYPE_NAME_AFTER_TYPE);
}

TestResult test_parser_expected_at_least_one_field(void)
{
    return expect_diagnostic("type Point end\n",
                             "a type declaration with no fields",
                             MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
}

TestResult test_parser_expected_field_name(void)
{
    return expect_diagnostic("type Point 13 : Int32 end\n",
                             "a field without a name",
                             MESSAGE_PARSER_EXPECTED_FIELD_NAME);
}

TestResult test_parser_expected_field_colon(void)
{
    return expect_diagnostic("type Point x Int32 end\n", "a field without ':'",
                             MESSAGE_PARSER_EXPECTED_FIELD_COLON);
}

TestResult test_parser_expected_field_type(void)
{
    return expect_diagnostic("type Point x : 13 end\n", "a field without a type",
                             MESSAGE_PARSER_EXPECTED_FIELD_TYPE);
}

TestResult test_parser_expected_field_or_end(void)
{
    return expect_diagnostic("type Point x : Int32 13 end\n",
                             "a field not followed by another field or 'end'",
                             MESSAGE_PARSER_EXPECTED_FIELD_OR_END);
}

TestResult test_parser_expected_argument(void)
{
    return expect_diagnostic("type P x: Int32 end\nvar p = P(+)\n",
                             "a missing call argument",
                             MESSAGE_PARSER_EXPECTED_ARGUMENT);
}

TestResult test_parser_expected_comma_or_rparen(void)
{
    return expect_diagnostic("type P x: Int32 end\nvar p = P(1 2)\n",
                             "constructor arguments without a separator",
                             MESSAGE_PARSER_EXPECTED_COMMA_OR_RPAREN);
}

TestResult test_parser_mixed_args(void)
{
    return expect_diagnostic("type P x: Int32 y: Int32 end\nvar p = P(1, y = 2)\n",
                             "mixed named and positional constructor arguments",
                             MESSAGE_PARSER_MIXED_ARGS);
}

TestResult test_parser_expected_function_name(void)
{
    return expect_diagnostic("fun (x:Int32):Int32 return x end\n",
                             "a function without a name",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_NAME);
}

TestResult test_parser_expected_function_lparen(void)
{
    return expect_diagnostic("fun f x:Int32):Int32 return x end\n",
                             "a function without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_LPAREN);
}

TestResult test_parser_expected_parameter_name(void)
{
    return expect_diagnostic("fun f(:Int32):Int32 return 1 end\n",
                             "a parameter without a name",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_NAME);
}

TestResult test_parser_expected_parameter_colon(void)
{
    return expect_diagnostic("fun f(x Int32):Int32 return x end\n",
                             "a parameter without ':'",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_COLON);
}

TestResult test_parser_expected_parameter_separator(void)
{
    return expect_diagnostic("fun f(x:Int32 y:Int32):Int32 return x end\n",
                             "parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_PARAMETER_SEPARATOR);
}

TestResult test_parser_expected_return_type(void)
{
    return expect_diagnostic("fun f(x:Int32) return x end\n",
                             "a function without a return type",
                             MESSAGE_PARSER_EXPECTED_RETURN_TYPE);
}

TestResult test_parser_expected_statement_or_end(void)
{
    return expect_diagnostic("fun f(x:Int32):Int32 x end\n",
                             "a non-statement in a function body",
                             MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TestResult test_parser_expected_end(void)
{
    return expect_diagnostic("fun f(x:Int32):Int32 return x\n",
                             "a function without 'end'",
                             MESSAGE_PARSER_EXPECTED_STATEMENT_OR_END);
}

TestResult test_parser_expected_then(void)
{
    return expect_diagnostic("fun f(x:Int32):Int32 if x > 0 return 1 end end\n",
                             "an if without 'then'",
                             MESSAGE_PARSER_EXPECTED_THEN);
}

TestResult test_parser_expected_rparen(void)
{
    return expect_diagnostic("var n : Int32 = (1 + 2\n",
                             "a grouped expression without ')'",
                             MESSAGE_PARSER_EXPECTED_RPAREN);
}

TestResult test_parser_expected_type(void)
{
    return expect_diagnostic("fun f(x:):Int32 return x end\n",
                             "a missing parameter type",
                             MESSAGE_PARSER_EXPECTED_TYPE);
}

TestResult test_parser_expected_function_type_lparen(void)
{
    return expect_diagnostic("var f : Fun<Int32> = 1\n",
                             "a function type without '('",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_LPAREN);
}

TestResult test_parser_expected_function_type_separator(void)
{
    return expect_diagnostic("var f : Fun<(Int32 Int32):Int32> = 1\n",
                             "function type parameters without a separator",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_SEPARATOR);
}

TestResult test_parser_expected_function_type_colon(void)
{
    return expect_diagnostic("var f : Fun<(Int32)> = 1\n",
                             "a function type without a return type colon",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_COLON);
}

TestResult test_parser_expected_function_type_gt(void)
{
    return expect_diagnostic("var f : Fun<(Int32):Int32 = 1\n",
                             "a function type without '>'",
                             MESSAGE_PARSER_EXPECTED_FUNCTION_TYPE_GT);
}

/* ---- checker messages (user-reachable) ---- */

TestResult test_checker_unknown_type(void)
{
    return expect_diagnostic("var x : Float = 13\n", "an unknown variable type",
                             MESSAGE_CHECKER_UNKNOWN_TYPE, "Float", "x");
}

TestResult test_checker_unknown_call(void)
{
    return expect_diagnostic("var p = Foo(1, 2)\n",
                             "an unknown call",
                             MESSAGE_CHECKER_UNKNOWN_CALL, "Foo");
}

TestResult test_checker_constructor_not_struct(void)
{
    return expect_diagnostic("var p = Int32(1)\n",
                             "constructing a non-struct type",
                             MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "Int32");
}

TestResult test_checker_constructor_arg_count(void)
{
    return expect_diagnostic("type P x: Int32 end\nvar p = P(1, 2)\n",
                             "a constructor with the wrong argument count",
                             MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                             "P", (size_t)1, (size_t)2);
}

TestResult test_checker_missing_constructor_arg(void)
{
    return expect_diagnostic("type P x: Int32 y: Int32 end\nvar p = P(x = 1)\n",
                             "a constructor missing a named argument",
                             MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG, "y");
}

TestResult test_checker_duplicate_constructor_arg(void)
{
    return expect_diagnostic(
        "type P x: Int32 y: Int32 end\nvar p = P(x = 1, x = 2, y = 3)\n",
        "a constructor with a duplicate named argument",
        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG, "x");
}

TestResult test_checker_unknown_constructor_field(void)
{
    return expect_diagnostic("type P x: Int32 end\nvar p = P(z = 1)\n",
                             "a constructor naming an unknown field",
                             MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD, "z");
}

TestResult test_checker_constructor_arg_type(void)
{
    return expect_diagnostic(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var p = P(f)\n",
        "a constructor argument with the wrong type",
        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE, (size_t)1, "P");
}

TestResult test_checker_initialiser_type_mismatch(void)
{
    return expect_diagnostic("type P x: Int32 end\nvar p : P = 5\n",
                             "an initialiser whose type disagrees with the annotation",
                             MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "p", "P");
}

TestResult test_checker_duplicate_type(void)
{
    return expect_diagnostic("type P x: Int32 end\ntype P y: Int32 end\n",
                             "a duplicated type declaration",
                             MESSAGE_CHECKER_DUPLICATE_TYPE, "P");
}

TestResult test_checker_redefines_int32(void)
{
    return expect_diagnostic("type Int32 x: Int32 end\n",
                             "a type declaration named Int32",
                             MESSAGE_CHECKER_REDEFINES_INT32);
}

TestResult test_checker_unknown_name(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  return y\nend\n",
        "an unknown name in a return expression",
        MESSAGE_CHECKER_UNKNOWN_NAME, "y");
}

TestResult test_checker_global_in_function(void)
{
    return expect_diagnostic(
        "var g : Int32 = 1\n"
        "fun f(x:Int32):Int32\n  return g\nend\n",
        "a program-level variable used inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "g");
}

TestResult test_checker_type_as_value(void)
{
    return expect_diagnostic(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return P\nend\n",
        "a type used as a value",
        MESSAGE_CHECKER_TYPE_AS_VALUE, "P");
}

TestResult test_checker_not_callable(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = 1\n"
        "var z : Int32 = y(1)\n",
        "calling a non-function value",
        MESSAGE_CHECKER_NOT_CALLABLE, "y");
}

TestResult test_checker_named_function_arg(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(x = 1)\n",
        "a named function argument",
        MESSAGE_CHECKER_NAMED_FUNCTION_ARG);
}

TestResult test_checker_function_arg_count(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var y : Int32 = f(1, 2)\n",
        "a function call with the wrong argument count",
        MESSAGE_CHECKER_FUNCTION_ARG_COUNT, "f", (size_t)1, (size_t)2);
}

TestResult test_checker_function_arg_type(void)
{
    return expect_diagnostic(
        "type P x:Int32 end\n"
        "fun f(p:P):Int32\n  return 1\nend\n"
        "var y : Int32 = f(1)\n",
        "a function call with the wrong argument type",
        MESSAGE_CHECKER_FUNCTION_ARG_TYPE, (size_t)1, "f");
}

TestResult test_checker_return_type_mismatch(void)
{
    return expect_diagnostic(
        "type P x:Int32 end\n"
        "fun f(x:Int32):P\n  return x\nend\n",
        "a return expression with the wrong type",
        MESSAGE_CHECKER_RETURN_TYPE_MISMATCH, "f");
}

TestResult test_checker_operator_type(void)
{
    return expect_diagnostic(
        "type P x:Int32 end\n"
        "fun f(p:P):P\n  return p + p\nend\n",
        "an operator applied to a non-Int32 value",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TestResult test_checker_field_type(void)
{
    return expect_diagnostic(
        "type Q x:Int32 end\ntype P y:Q end\n",
        "a non-Int32 field type",
        MESSAGE_CHECKER_FIELD_TYPE, "y");
}

TestResult test_checker_duplicate_name(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var f : Int32 = 1\n",
        "a duplicate top-level name",
        MESSAGE_CHECKER_DUPLICATE_NAME, "f");
}

TestResult test_checker_duplicate_parameter(void)
{
    return expect_diagnostic(
        "fun f(x:Int32, x:Int32):Int32\n  return x\nend\n",
        "a duplicate parameter",
        MESSAGE_CHECKER_DUPLICATE_PARAMETER, "x");
}

TestResult test_checker_condition_type(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n"
        "  if x then\n    return 1\n  end\n"
        "  return 0\n"
        "end\n",
        "an Int32 if condition",
        MESSAGE_CHECKER_CONDITION_TYPE);
}

TestResult test_checker_break_outside_loop(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  break\n  return x\nend\n",
        "break outside a loop",
        MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP);
}

TestResult test_checker_continue_outside_loop(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n  continue\n  return x\nend\n",
        "continue outside a loop",
        MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
}

TestResult test_checker_function_may_not_return(void)
{
    return expect_diagnostic(
        "fun f(x:Int32):Int32\n"
        "  if x > 0 then\n    return 1\n  end\n"
        "end\n",
        "a function with a missing return path",
        MESSAGE_CHECKER_FUNCTION_MAY_NOT_RETURN, "f");
}

TestResult test_checker_redefines_bool(void)
{
    return expect_diagnostic("type Bool x:Int32 end\n",
                             "a type declaration named Bool",
                             MESSAGE_CHECKER_REDEFINES_BOOL);
}

/* ---- driver messages ---- */

TestResult test_driver_could_not_open_source(void)
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = run_compiler("/nonexistent/nope.nip", "tests/.test-bin",
                          "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  a nonexistent source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_COULD_NOT_OPEN_SOURCE, "/nonexistent/nope.nip");
    if (!file_has_content("tests/.test.log") ||
        !file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing \"could not open source file\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_driver_source_too_large(void)
{
    char big[4097];
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    memset(big, 'a', sizeof(big));
    if (!write_bytes("tests/.test.nip", (const unsigned char *)big,
                     sizeof(big))) {
        fprintf(stderr, "  could not write the oversized source\n");
        goto finished;
    }
    status = run_compiler("tests/.test.nip", "tests/.test-bin", "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  an oversized source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_SOURCE_TOO_LARGE, "tests/.test.nip", 4096);
    if (!file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing \"source is larger than\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_driver_source_contains_nul(void)
{
    static const unsigned char data[] = {
        'v', 'a', 'r', ' ', 'x', ' ', ':', ' ', 'I', 'n', 't', '3', '2',
        ' ', '=', ' ', '1', '\0', '\n'
    };
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!write_bytes("tests/.test.nip", data, sizeof(data))) {
        fprintf(stderr, "  could not write the NUL-containing source\n");
        goto finished;
    }
    status = run_compiler("tests/.test.nip", "tests/.test-bin", "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  a NUL-containing source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_SOURCE_CONTAINS_NUL, "tests/.test.nip");
    if (!file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing \"source contains a NUL byte\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_driver_failed_to_read(void)
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = run_compiler("tests", "tests/.test-bin", "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  a directory source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected), MESSAGE_DRIVER_FAILED_TO_READ,
                   "tests");
    if (!file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing \"failed to read source file\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_driver_usage(void)
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    status = run_command("./bin/nippy", "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  no-argument invocation unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected), MESSAGE_DRIVER_USAGE,
                   "./bin/nippy");
    if (!file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing usage diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

TestResult test_driver_could_not_write_header(void)
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!write_text("tests/.test.nip", "var x : Int32 = 13\n")) {
        fprintf(stderr, "  could not write the source\n");
        goto finished;
    }
    status = run_compiler("tests/.test.nip", "/nonexistent-dir/out",
                          "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  an unwritable output path unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_COULD_NOT_WRITE_HEADER, "/nonexistent-dir/out.h");
    if (!file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing \"could not write generated header\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
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
        fprintf(stderr, "  unknown node kind did not produce \"unsupported AST node\"\n");
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

/* ---- allocation-failure coverage ---- */

/* Run the compiler executable with an allocation failure armed at `index`,
   capturing its output. */
static int run_with_alloc_failure(size_t index, const char *source_path,
                                  const char *binary, const char *log)
{
    char command[640];

    snprintf(command, sizeof(command),
             "NIPPY_FAIL_ALLOC_AFTER=%zu ./bin/nippy %s %s > %s 2>&1",
             index, source_path, binary, log);
    return system(command);
}

static const char *const oom_source =
    "type Point\n"
    "  x: Int32\n"
    "  y: Int32\n"
    "end\n"
    "\n"
    "fun add(x:Int32, y:Int32):Int32\n"
    "  return x + y\n"
    "end\n"
    "\n"
    "fun positive(x:Int32):Bool\n"
    "  if x > 0 then\n"
    "    return true\n"
    "  else\n"
    "    return false\n"
    "  end\n"
    "end\n"
    "\n"
    "fun loop(x:Int32):Int32\n"
    "  while x > 0\n"
    "    break\n"
    "  end\n"
    "  return x\n"
    "end\n"
    "\n"
    "var p1: Point = Point(1, 2)\n"
    "var p2: Point = Point(x = 3, y = 4)\n"
    "var n = add(7, 8)\n"
    "var ok : Bool = positive(n)\n"
    "var m = loop(n)\n"
    "var f : Fun<(Int32, Int32):Int32> = add\n";

/* Every allocation site must fail closed: a non-zero exit with a message on
   stderr, never a crash or a silently truncated artifact. */
TestResult test_allocation_failures_fail_closed(void)
{
    size_t index;
    TestResult result = TEST_ERROR;

    cleanup();
    if (!write_text("tests/.test.nip", oom_source)) {
        fprintf(stderr, "  could not write the OOM source\n");
        goto finished;
    }
    for (index = 0; index < 400; index++) {
        int status;

        remove("tests/.test-bin");
        status = run_with_alloc_failure(index, "tests/.test.nip",
                                        "tests/.test-bin", "tests/.test.log");

        if (status == -1) {
            fprintf(stderr, "  could not launch the compiler at index %zu\n",
                    index);
            goto finished;
        }
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "  compiler crashed at allocation index %zu\n", index);
            result = TEST_FAIL;
            goto finished;
        }
        if (WEXITSTATUS(status) != 0 && !file_has_content("tests/.test.log")) {
            fprintf(stderr, "  no diagnostic at allocation index %zu\n", index);
            result = TEST_FAIL;
            goto finished;
        }
        if (WEXITSTATUS(status) != 0 && access("tests/.test-bin", F_OK) == 0) {
            fprintf(stderr, "  a failed build left a binary at index %zu\n", index);
            result = TEST_FAIL;
            goto finished;
        }
        /* Once the failure index is past every allocation the build succeeds
           again; stop sweeping. */
        if (WEXITSTATUS(status) == 0 && index > 20) {
            break;
        }
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}

/* The sweep above proves fail-closed behaviour; this proves the individual
   out-of-memory messages are actually reachable. */
TestResult test_allocation_failure_messages(void)
{
    char expected[10][256];
    size_t expected_count = 0;
    size_t index;
    size_t i;
    TestResult result = TEST_ERROR;

    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_DRIVER_COULD_NOT_ALLOCATE_BUFFER);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_DRIVER_COULD_NOT_ALLOCATE_PATHS);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_LEXER_OUT_OF_MEMORY);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_PARSER_OUT_OF_MEMORY_RECORDING_NAME);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_PARSER_OUT_OF_MEMORY_BUILDING_TREE);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_TYPING);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_COPYING_NAME);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_CHECKER_OUT_OF_MEMORY_PROGRAM);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_GENERATOR_OUT_OF_MEMORY);
    expect_message(expected[expected_count++], sizeof(expected[0]),
                   MESSAGE_DRIVER_COULD_NOT_ALLOCATE_COMMAND);

    cleanup();
    if (!write_text("tests/.test.nip", oom_source)) {
        fprintf(stderr, "  could not write the OOM source\n");
        goto finished;
    }

    for (i = 0; i < expected_count; i++) {
        int seen = 0;

        for (index = 0; index < 400 && !seen; index++) {
            int status = run_with_alloc_failure(index, "tests/.test.nip",
                                                "tests/.test-bin",
                                                "tests/.test.log");

            if (status == -1 || WIFSIGNALED(status)) {
                fprintf(stderr, "  compiler failed to run at index %zu\n", index);
                goto finished;
            }
            seen = file_contains("tests/.test.log", expected[i]);
        }
        if (!seen) {
            fprintf(stderr, "  never produced \"%s\"\n", expected[i]);
            result = TEST_FAIL;
            goto finished;
        }
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
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

/* ---- remaining driver failures ---- */

TestResult test_driver_could_not_write_source(void)
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!write_text("tests/.test.nip", "var x : Int32 = 13\n")) {
        fprintf(stderr, "  could not write the source\n");
        goto finished;
    }
    /* A directory occupying the .c path lets the header write succeed and the
       C write fail. */
    if (system("mkdir -p tests/.test-bin.c") != 0) {
        fprintf(stderr, "  could not create the blocking directory\n");
        goto finished;
    }
    status = run_compiler("tests/.test.nip", "tests/.test-bin", "tests/.test.log");
    if (status == 0) {
        fprintf(stderr, "  an unwritable C path unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finish_cleanup;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_COULD_NOT_WRITE_SOURCE, "tests/.test-bin.c");
    if (!file_contains("tests/.test.log", expected)) {
        fprintf(stderr, "  missing \"could not write generated C source\"\n");
        result = TEST_FAIL;
        goto finish_cleanup;
    }
    result = TEST_PASS;

finish_cleanup:
    system("rmdir tests/.test-bin.c 2>/dev/null");
    cleanup();
    return result;

finished:
    cleanup();
    return result;
}

TestResult test_driver_cc_failures(void)
{
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!write_text("tests/.test.nip", "var x : Int32 = 13\n")) {
        fprintf(stderr, "  could not write the source\n");
        goto finished;
    }
    /* An empty PATH leaves `cc` unfindable, so system() reports a failure to
       start the shell command. */
    status = system("PATH=/nonexistent ./bin/nippy tests/.test.nip "
                    "tests/.test-bin > tests/.test.log 2>&1");
    if (status == 0) {
        fprintf(stderr, "  a missing C compiler unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains("tests/.test.log", MESSAGE_DRIVER_COULD_NOT_START_CC) &&
        !file_contains("tests/.test.log", MESSAGE_DRIVER_CC_FAILED)) {
        fprintf(stderr, "  missing C compiler failure diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    cleanup();
    return result;
}
