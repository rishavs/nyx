#include "test_harness.h"
#include "compiler.h"
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const temporary_files[] = {
    "tests/.test.nip",
    "tests/.test-bin",
    "tests/.test-bin.c",
    "tests/.test-bin.h",
    "tests/.test.log",
    "tests/.test-runtime",
    "tests/.test-runtime.c",
    "tests/.test-runtime.h",
    "tests/.test-runtime.log",
    "tests/.test-runtime-main.c",
    "tests/.test-runtime.o",
    "tests/.test-runtime-main.o"
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

/* This suite is the only one that touches files, because file I/O and the cc
   invocation are what these tests exercise. */
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

static int file_equals(const char *path, const char *expected)
{
    FILE *file = fopen(path, "r");
    char buffer[1024];
    size_t length;
    int ok;

    if (file == NULL) {
        return 0;
    }
    length = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[length] = '\0';
    ok = ferror(file) == 0 && fgetc(file) == EOF &&
         strcmp(buffer, expected) == 0;
    fclose(file);
    return ok;
}

/* Compile `source` in memory, build the generated C, run it, and compare
   stdout byte-for-byte. Tag tests and narrowing are runtime behaviour, so
   golden C alone cannot prove them. */
static TestResult run_generated(const char *source, const char *expected,
                                const char *what)
{
    CompilerConfig config = {.header_name = ".test-runtime.h",
                             .source_name = "test.nip"};
    CompilerResult compiled = compiler_compile(source, &config);
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!compiled.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                compiled.error_message == NULL ? "(no diagnostic)"
                                               : compiled.error_message);
        goto finished;
    }
    if (!write_text("tests/.test-runtime.h", compiled.h_source) ||
        !write_text("tests/.test-runtime.c", compiled.c_source)) {
        fprintf(stderr, "  %s: could not write the generated C\n", what);
        goto finished;
    }
    status = run_command(
        "cc -std=c11 -o tests/.test-runtime tests/.test-runtime.c",
        "tests/.test-runtime.log");
    if (status != 0) {
        fprintf(stderr, "  %s: generated C did not build\n", what);
        goto finished;
    }
    status = run_command("tests/.test-runtime", "tests/.test-runtime.log");
    if (status != 0) {
        fprintf(stderr, "  %s: program exited non-zero\n", what);
        goto finished;
    }
    if (!file_equals("tests/.test-runtime.log", expected)) {
        fprintf(stderr, "  %s: output did not match \"%s\"\n", what, expected);
        goto finished;
    }
    result = TEST_PASS;

finished:
    compiler_result_free(&compiled);
    cleanup();
    return result;
}

/* Like run_generated, but links a small driver translation unit so runtime
   behaviour can be observed through a plain C entry point. The driver gets the
   generated header and may call Nippy functions by their source names. */
static TestResult run_generated_driver(const char *source, const char *driver,
                                       const char *expected, const char *what)
{
    CompilerConfig config = {.header_name = ".test-runtime.h",
                             .source_name = "test.nip"};
    CompilerResult compiled = compiler_compile(source, &config);
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!compiled.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                compiled.error_message == NULL ? "(no diagnostic)"
                                               : compiled.error_message);
        goto finished;
    }
    if (!write_text("tests/.test-runtime.h", compiled.h_source) ||
        !write_text("tests/.test-runtime.c", compiled.c_source) ||
        !write_text("tests/.test-runtime-main.c", driver)) {
        fprintf(stderr, "  %s: could not write the generated C\n", what);
        goto finished;
    }
    /* The generated translation unit always defines main; rename it so the
       driver's main can call the Nippy functions. */
    status = run_command(
        "cc -std=c11 -Dmain=nippy_generated_main -c tests/.test-runtime.c "
        "-o tests/.test-runtime.o",
        "tests/.test-runtime.log");
    if (status == 0) {
        status = run_command(
            "cc -std=c11 -c tests/.test-runtime-main.c "
            "-o tests/.test-runtime-main.o",
            "tests/.test-runtime.log");
    }
    if (status == 0) {
        status = run_command(
            "cc -o tests/.test-runtime tests/.test-runtime.o "
            "tests/.test-runtime-main.o",
            "tests/.test-runtime.log");
    }
    if (status != 0) {
        fprintf(stderr, "  %s: generated C did not build\n", what);
        goto finished;
    }
    status = run_command("tests/.test-runtime", "tests/.test-runtime.log");
    if (status != 0) {
        fprintf(stderr, "  %s: program exited non-zero\n", what);
        goto finished;
    }
    if (!file_equals("tests/.test-runtime.log", expected)) {
        fprintf(stderr, "  %s: output did not match \"%s\"\n", what, expected);
        goto finished;
    }
    result = TEST_PASS;

finished:
    compiler_result_free(&compiled);
    cleanup();
    return result;
}

/* ---- runtime semantics ---- */

TestResult test_runtime_program_prints_value(void)
{
    return run_generated("var x : Int32 = 13\n", "13\n", "program 13");
}

TestResult test_runtime_union_narrowing(void)
{
    const char *source =
        "fun make(x: Int32): Int32 | Nil\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if v.is<Int32>() then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "\n"
        "fun compute(x: Int32): Int32\n"
        "  return unwrap(make(x))\n"
        "end\n";
    const char *driver =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t compute(int32_t x);\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)compute(3));\n"
        "    return 0;\n"
        "}\n";

    return run_generated_driver(source, driver, "3\n", "union narrowing");
}

TestResult test_runtime_generic_instantiations(void)
{
    const char *source =
        "fun identity<T>(x: T): T\n"
        "  return x\n"
        "end\n"
        "\n"
        "fun flag(): Bool\n"
        "  return identity(true)\n"
        "end\n"
        "\n"
        "fun compute(x: Int32): Int32\n"
        "  if flag() then\n"
        "    return identity(x)\n"
        "  end\n"
        "  return 0\n"
        "end\n";
    const char *driver =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "int32_t compute(int32_t x);\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d\\n\", (int)compute(3));\n"
        "    return 0;\n"
        "}\n";

    return run_generated_driver(source, driver, "3\n",
                                "generic instantiations");
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
    "type Something = Int32 | Bool | Nil\n"
    "\n"
    "type Pair<A, B>\n"
    "  first: A\n"
    "  second: B\n"
    "end\n"
    "\n"
    "fun identity<T>(x: T): T\n"
    "  return x\n"
    "end\n"
    "\n"
    "fun classify(x: Int32 | Bool | Nil): Int32\n"
    "  if x.is<Int32>() then\n"
    "    return x\n"
    "  elseif x.is<Bool>() then\n"
    "    if x then return 1 else return 0 end\n"
    "  end\n"
    "  return 0\n"
    "end\n"
    "\n"
    "fun add(x:Int32, y:Int32):Int32\n"
    "  return x + y\n"
    "end\n"
    "\n"
    "var p: Something = 3\n"
    "var q: Point = Point(1, 2)\n"
    "var r = Pair(1, true)\n"
    "var s: Int32 = identity(7)\n"
    "var t: Int32 = classify(true)\n"
    "var u: Int32 = add(1, 2)\n"
    "var arena: Store = Store(16)\n"
    "var stored: Handle<Int32> = arena.alloc<Int32>(1)\n"
    "var byte_value: Byte = 7\n";

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


/* Compile and run a generated program that is expected to fail at runtime,
   checking its diagnostic. */
static TestResult run_generated_failure(const char *source,
                                        const char *expected,
                                        const char *what)
{
    CompilerConfig config = {.header_name = ".test-runtime.h",
                             .source_name = "test.nip"};
    CompilerResult compiled = compiler_compile(source, &config);
    TestResult result = TEST_ERROR;
    int status;

    cleanup();
    if (!compiled.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                compiled.error_message == NULL ? "(no diagnostic)"
                                               : compiled.error_message);
        goto finished;
    }
    if (!write_text("tests/.test-runtime.h", compiled.h_source) ||
        !write_text("tests/.test-runtime.c", compiled.c_source)) {
        fprintf(stderr, "  %s: could not write the generated C\n", what);
        goto finished;
    }
    status = run_command(
        "cc -std=c11 -o tests/.test-runtime tests/.test-runtime.c",
        "tests/.test-runtime.log");
    if (status != 0) {
        fprintf(stderr, "  %s: generated C did not build\n", what);
        goto finished;
    }
    status = run_command("tests/.test-runtime", "tests/.test-runtime.log");
    if (status == 0) {
        fprintf(stderr, "  %s: program unexpectedly succeeded\n", what);
        goto finished;
    }
    if (!file_contains("tests/.test-runtime.log", expected)) {
        fprintf(stderr, "  %s: missing runtime diagnostic\n", what);
        goto finished;
    }
    result = TEST_PASS;

finished:
    compiler_result_free(&compiled);
    cleanup();
    return result;
}

TestResult test_runtime_store_alloc_read(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "var n: Int32 = s.alloc<Int32>(42).read()\n";

    return run_generated(source, "42\n", "store alloc/read");
}

TestResult test_runtime_store_write(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun roundtrip(h: Handle<Int32>): Int32\n"
        "  h.write(42)\n"
        "  return h.read()\n"
        "end\n"
        "var n: Int32 = roundtrip(s.alloc<Int32>(0))\n";

    return run_generated(source, "42\n", "store write");
}

TestResult test_runtime_store_byte(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "fun byte_ok(): Int32\n"
        "  if s.alloc<Byte>(255).read() == 255 then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = byte_ok()\n";

    return run_generated(source, "1\n", "byte allocation");
}

TestResult test_runtime_store_growth(void)
{
    const char *source =
        "var s: Store = Store(1)\n"
        "var a0: Handle<Int32> = s.alloc<Int32>(0)\n"
        "var a1: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var a2: Handle<Int32> = s.alloc<Int32>(2)\n"
        "var a3: Handle<Int32> = s.alloc<Int32>(3)\n"
        "var a4: Handle<Int32> = s.alloc<Int32>(4)\n"
        "var a5: Handle<Int32> = s.alloc<Int32>(5)\n"
        "var a6: Handle<Int32> = s.alloc<Int32>(6)\n"
        "var a7: Handle<Int32> = s.alloc<Int32>(7)\n"
        "var a8: Handle<Int32> = s.alloc<Int32>(8)\n"
        "var a9: Handle<Int32> = s.alloc<Int32>(9)\n"
        "var a10: Handle<Int32> = s.alloc<Int32>(10)\n"
        "var a11: Handle<Int32> = s.alloc<Int32>(11)\n"
        "var a12: Handle<Int32> = s.alloc<Int32>(12)\n"
        "var a13: Handle<Int32> = s.alloc<Int32>(13)\n"
        "var a14: Handle<Int32> = s.alloc<Int32>(14)\n"
        "var a15: Handle<Int32> = s.alloc<Int32>(15)\n"
        "var a16: Handle<Int32> = s.alloc<Int32>(16)\n"
        "var a17: Handle<Int32> = s.alloc<Int32>(17)\n"
        "var a18: Handle<Int32> = s.alloc<Int32>(18)\n"
        "var a19: Handle<Int32> = s.alloc<Int32>(19)\n"
        "var n: Int32 = a19.read()\n";

    return run_generated(source, "", "store growth");
}

TestResult test_runtime_stale_handle(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var h: Handle<Int32> = s.alloc<Int32>(1)\n"
        "s.reset()\n"
        "var n: Int32 = h.read()\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale handle");
}


TestResult test_runtime_two_stores(void)
{
    const char *source =
        "var a: Store = Store(1)\n"
        "var b: Store = Store(1)\n"
        "var n: Int32 = a.alloc<Int32>(1).read() + "
        "b.alloc<Int32>(2).read()\n";

    return run_generated(source, "3\n", "two stores");
}


TestResult test_runtime_string_ops(void)
{
    const char *source =
        "fun check(s: String<16>): Int32\n"
        "  if s == \"abc\" then\n"
        "    return s.byte_len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(\"abc\")\n";

    return run_generated(source, "3\n", "string length and equality");
}

TestResult test_runtime_string_bounds(void)
{
    const char *source =
        "fun bad(s: String<4>): Byte\n"
        "  return s.byte_at(5)\n"
        "end\n"
        "var b: Byte = bad(\"abc\")\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STRING_BOUNDS,
                                 "string bounds");
}

TestResult test_runtime_string_union(void)
{
    const char *source =
        "fun len_or_zero(u: String<16> | Nil): Int32\n"
        "  if u.is<String<16>>() then\n"
        "    return u.byte_len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = len_or_zero(\"hey\")\n";

    return run_generated(source, "3\n", "string union narrowing");
}

TestResult test_runtime_error_fields(void)
{
    const char *source =
        "fun describe(e: Error | Nil): Int32\n"
        "  if e.is<Error>() then\n"
        "    return e.line()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = describe("
        "Error(\"E\", \"d\", \"f\", 7, 2, 3))\n";

    return run_generated(source, "7\n", "Error fields");
}

TestResult test_runtime_error_in_store(void)
{
    const char *source =
        "fun line_of(h: Handle<Error>): Int32\n"
        "  return h.read().line()\n"
        "end\n"
        "var arena: Store = Store(4096)\n"
        "var n: Int32 = line_of(arena.alloc<Error>("
        "Error(\"E\", \"d\", \"f\", 9, 2, 3)))\n";

    return run_generated(source, "9\n", "Error in a store");
}

TestResult test_runtime_string_in_store(void)
{
    const char *source =
        "fun len_of(h: Handle<String<16>>): Int32\n"
        "  return h.read().byte_len()\n"
        "end\n"
        "var arena: Store = Store(32)\n"
        "var n: Int32 = len_of(arena.alloc<String<16>>(\"hi\"))\n";

    return run_generated(source, "2\n", "String in a store");
}


TestResult test_runtime_assignment_top(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n";

    return run_generated(source, "2\n", "top-level assignment");
}

TestResult test_runtime_assignment_parameter(void)
{
    const char *source =
        "fun bump(x: Int32): Int32\n"
        "  x = x + 1\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = bump(1)\n";

    return run_generated(source, "2\n", "parameter assignment");
}

TestResult test_runtime_assignment_string(void)
{
    const char *source =
        "fun replace(s: String<8>): String<8>\n"
        "  s = \"bye\"\n"
        "  return s\n"
        "end\n"
        "var n: Int32 = replace(\"hi\").byte_len()\n";

    return run_generated(source, "3\n", "string assignment");
}

TestResult test_runtime_assignment_union_narrowing(void)
{
    const char *source =
        "fun check(x: Int32 | Bool): Int32\n"
        "  if x.is<Int32>() then\n"
        "    x = true\n"
        "    if x.is<Bool>() then\n"
        "      return 1\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1)\n";

    return run_generated(source, "1\n", "assignment invalidates narrowing");
}

TestResult test_runtime_assignment_byte(void)
{
    const char *source =
        "fun check(b: Byte): Int32\n"
        "  b = 200\n"
        "  if b == 200 then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1)\n";

    return run_generated(source, "1\n", "byte assignment");
}

TestResult test_runtime_assignment_error(void)
{
    const char *source =
        "fun line_after(e: Error): Int32\n"
        "  e = Error(\"New\", \"d\", \"f\", 42, 2, 3)\n"
        "  return e.line()\n"
        "end\n"
        "var n: Int32 = line_after("
        "Error(\"Old\", \"d\", \"f\", 1, 2, 3))\n";

    return run_generated(source, "42\n", "Error assignment");
}

TestResult test_runtime_assignment_handle(void)
{
    const char *source =
        "var arena: Store = Store(16)\n"
        "fun swap(h: Handle<Int32>, other: Handle<Int32>): Int32\n"
        "  h = other\n"
        "  return h.read()\n"
        "end\n"
        "fun choose(): Int32\n"
        "  return swap(arena.alloc<Int32>(1), arena.alloc<Int32>(2))\n"
        "end\n"
        "var n: Int32 = choose()\n";

    return run_generated(source, "2\n", "handle assignment");
}


TestResult test_runtime_assignment_loop(void)
{
    const char *source =
        "fun count(x: Int32): Int32\n"
        "  while x < 3\n"
        "    x = x + 1\n"
        "  end\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = count(0)\n";

    return run_generated(source, "3\n", "assignment in a loop");
}


TestResult test_runtime_assignment_repeated(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "x = 2\n"
        "x = 3\n";

    return run_generated(source, "3\n", "repeated assignment");
}


TestResult test_runtime_assignment_generic(void)
{
    const char *source =
        "fun choose<T>(x: T, y: T): T\n"
        "  x = y\n"
        "  return x\n"
        "end\n"
        "var n: Int32 = choose(1, 2)\n";

    return run_generated(source, "2\n", "generic assignment");
}


TestResult test_runtime_assignment_inferred(void)
{
    const char *source =
        "var x = 1\n"
        "x = 2\n";

    return run_generated(source, "2\n", "inferred assignment");
}


TestResult test_runtime_assignment_nested_narrowing(void)
{
    const char *source =
        "fun check(x: Int32 | Bool, y: Int32 | Bool): Int32\n"
        "  if x.is<Int32>() then\n"
        "    if y.is<Int32>() then\n"
        "      x = true\n"
        "      y = true\n"
        "      if x.is<Bool>() && y.is<Bool>() then\n"
        "        return 1\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = check(1, 2)\n";

    return run_generated(source, "1\n", "nested narrowing invalidation");
}


TestResult test_runtime_assignment_union(void)
{
    const char *source =
        "fun set(u: Int32 | Bool): Int32\n"
        "  u = true\n"
        "  if u.is<Bool>() then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = set(1)\n";

    return run_generated(source, "1\n", "union assignment");
}


TestResult test_runtime_global_store(void)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "fun read_global(): Int32\n"
        "  return arena.alloc<Int32>(7).read()\n"
        "end\n"
        "var n: Int32 = read_global()\n";

    return run_generated(source, "7\n", "global store");
}


TestResult test_runtime_dynamic_strings(void)
{
    const char *source =
        "global arena: Store = Store(256)\n"
        "fun len(s: String): Int32\n"
        "  return s.byte_len()\n"
        "end\n"
        "fun fixed_len(f: String<8>): Int32\n"
        "  return arena.string(f).byte_len()\n"
        "end\n"
        "fun is_world(s: String): Int32\n"
        "  if s.slice(6, 5) == \"world\" then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = len(\"hello\") +\n"
        "  len(arena.concat(\"hello\", \" world\")) +\n"
        "  fixed_len(\"hi\") +\n"
        "  is_world(\"hello world\") +\n"
        "  len(arena.copy(\"hi\"))\n";

    return run_generated(source, "21\n", "dynamic strings");
}

TestResult test_runtime_dynamic_string_in_handle(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun first(h: Handle<String>): Int32\n"
        "  return h.read().byte_len()\n"
        "end\n"
        "var n: Int32 = first(arena.alloc<String>(\"hi\"))\n";

    return run_generated(source, "2\n", "dynamic String in a handle");
}

TestResult test_runtime_dynamic_string_stale(void)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var s: String = arena.copy(\"hi\")\n"
        "arena.reset()\n"
        "var b: Byte = s.byte_at(0)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic string");
}


TestResult test_runtime_dynamic_string_bounds(void)
{
    const char *source =
        "fun at(s: String): Byte\n"
        "  return s.byte_at(10)\n"
        "end\n"
        "var b: Byte = at(\"hi\")\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STRING_BOUNDS,
                                 "dynamic string bounds");
}

TestResult test_runtime_dynamic_string_stale_len(void)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var s: String = arena.copy(\"hi\")\n"
        "arena.reset()\n"
        "var n: Int32 = s.byte_len()\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic string length");
}


TestResult test_runtime_dynamic_string_union(void)
{
    const char *source =
        "fun len_or_zero(u: String | Nil): Int32\n"
        "  if u.is<String>() then\n"
        "    return u.byte_len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = len_or_zero(\"hi\")\n";

    return run_generated(source, "2\n", "dynamic String union");
}


TestResult test_runtime_string_builder(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun fill(b: StringBuilder): Int32\n"
        "  b.append(\"hello\")\n"
        "  b.append_byte(44)\n"
        "  b.append(\" world\")\n"
        "  return b.byte_len() + b.finish().byte_len()\n"
        "end\n"
        "var n: Int32 = fill(arena.builder(2))\n";

    return run_generated(source, "24\n", "StringBuilder");
}

TestResult test_runtime_string_builder_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var b: StringBuilder = arena.builder(4)\n"
        "b.append(\"hi\")\n"
        "arena.reset()\n"
        "b.append(\"x\")\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale StringBuilder");
}


TestResult test_runtime_string_builder_content(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun content(b: StringBuilder): Int32\n"
        "  b.append(\"hello\")\n"
        "  b.append_byte(44)\n"
        "  b.append(\" world\")\n"
        "  if b.finish() == \"hello, world\" then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = content(arena.builder(1))\n";

    return run_generated(source, "1\n", "StringBuilder content");
}


TestResult test_runtime_string_builder_reuse(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun reuse(b: StringBuilder): Int32\n"
        "  b.append(\"hi\")\n"
        "  if b.finish() == \"hi\" then\n"
        "    b.append(\"!\")\n"
        "    if b.finish() == \"hi!\" then\n"
        "      return 1\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = reuse(arena.builder(2))\n";

    return run_generated(source, "1\n", "StringBuilder reuse after finish");
}


TestResult test_runtime_fixed_list(void)
{
    const char *source =
        "fun score(xs: List<Int32, 3>): Int32\n"
        "  return xs.get(0) + xs.get(1) + xs.get(2) + xs.len()\n"
        "end\n"
        "var n: Int32 = score([1, 2, 3])\n";

    return run_generated(source, "9\n", "fixed list");
}

TestResult test_runtime_dynamic_list(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun fill(xs: List<Int32>): Int32\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  xs.set(0, 10)\n"
        "  return xs.get(0) + xs.get(1) + xs.len()\n"
        "end\n"
        "var n: Int32 = fill(arena.list<Int32>(1))\n";

    return run_generated(source, "14\n", "dynamic list");
}

TestResult test_runtime_list_bounds(void)
{
    const char *source =
        "fun at(xs: List<Int32, 1>): Int32\n"
        "  return xs.get(1)\n"
        "end\n"
        "var n: Int32 = at([1])\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_LIST_BOUNDS,
                                 "fixed list bounds");
}

TestResult test_runtime_list_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(1)\n"
        "arena.reset()\n"
        "xs.push(2)\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale dynamic list");
}


TestResult test_runtime_fixed_list_zero(void)
{
    const char *source =
        "fun count(xs: List<Int32, 0>): Int32\n"
        "  return xs.len()\n"
        "end\n"
        "var n: Int32 = count([])\n";

    return run_generated(source, "0\n", "zero-length fixed list");
}

TestResult test_runtime_fixed_list_strings(void)
{
    const char *source =
        "fun count(xs: List<String<8>, 2>): Int32\n"
        "  if xs.get(0) == \"a\" then\n"
        "    return xs.len()\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "var n: Int32 = count([\"a\", \"b\"])\n";

    return run_generated(source, "2\n", "fixed list of strings");
}


TestResult test_runtime_two_dynamic_lists(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun sizes(a: List<Int32>, b: List<Bool>): Int32\n"
        "  a.push(1)\n"
        "  b.push(true)\n"
        "  return a.len() + b.len()\n"
        "end\n"
        "var n: Int32 = sizes(arena.list<Int32>(1), arena.list<Bool>(1))\n";

    return run_generated(source, "2\n", "two dynamic list element types");
}


TestResult test_runtime_inferred_list(void)
{
    const char *source =
        "var n: Int32 = [1, 2, 3].get(0) + [1, 2, 3].len()\n";

    return run_generated(source, "4\n", "inferred list literal");
}


TestResult test_runtime_fixed_list_mutation(void)
{
    const char *source =
        "fun ops(xs: List<Int32, 3>): Int32\n"
        "  xs.push(2)\n"
        "  xs.push(3)\n"
        "  xs[0] = 10\n"
        "  return xs.len() + xs[0]\n"
        "end\n"
        "var n: Int32 = ops([1])\n";

    return run_generated(source, "13\n", "fixed list push/index");
}

TestResult test_runtime_dynamic_list_mutation(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun ops(xs: List<Int32>): Int32\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  xs[0] = 10\n"
        "  return xs.len() + xs[0]\n"
        "end\n"
        "var n: Int32 = ops(arena.list<Int32>(1))\n";

    return run_generated(source, "12\n", "dynamic list push/index");
}


TestResult test_runtime_list_pop_value(void)
{
    const char *source =
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if v.is<Int32>() then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun pop_value(xs: List<Int32, 3>): Int32\n"
        "  return unwrap(xs.pop())\n"
        "end\n"
        "var n: Int32 = pop_value([1, 2, 3])\n";

    return run_generated(source, "3\n", "list pop value");
}

TestResult test_runtime_list_pop_empty(void)
{
    const char *source =
        "fun unwrap(v: Int32 | Nil): Int32\n"
        "  if v.is<Int32>() then\n"
        "    return v\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun empty(xs: List<Int32, 1>): Int32\n"
        "  return unwrap(xs.pop())\n"
        "end\n"
        "var n: Int32 = empty([])\n";

    return run_generated(source, "0\n", "empty list pop");
}

TestResult test_runtime_fixed_list_full(void)
{
    const char *source =
        "fun full(xs: List<Int32, 1>): Int32\n"
        "  xs.push(1)\n"
        "  xs.push(2)\n"
        "  return xs.len()\n"
        "end\n"
        "var n: Int32 = full([])\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_LIST_BOUNDS,
                                 "fixed list capacity");
}


TestResult test_runtime_list_union_and_function(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "fun count(xs: List<Int32 | Bool, 2>,\n"
        "           fs: List<Fun<(Int32):Int32>>): Int32\n"
        "  fs.push(inc)\n"
        "  return xs.len() + fs.len()\n"
        "end\n"
        "var n: Int32 = count([1, true],\n"
        "  arena.list<Fun<(Int32):Int32>>(1))\n";

    return run_generated(source, "3\n", "list unions and functions");
}


TestResult test_runtime_function_list_pop(void)
{
    const char *source =
        "fun inc(x: Int32): Int32\n"
        "  return x + 1\n"
        "end\n"
        "fun is_fn(v: Fun<(Int32):Int32> | Nil): Int32\n"
        "  if v.is<Fun<(Int32):Int32>>() then\n"
        "    return 1\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun pop_fn(xs: List<Fun<(Int32):Int32>, 1>): Int32\n"
        "  return is_fn(xs.pop())\n"
        "end\n"
        "var n: Int32 = pop_fn([inc])\n";

    return run_generated(source, "1\n", "fixed function list pop");
}

TestResult test_runtime_union_list_pop(void)
{
    const char *source =
        "fun unwrap(v: Int32 | Bool | Nil): Int32\n"
        "  if v.is<Bool>() then\n"
        "    return 2\n"
        "  end\n"
        "  return 0\n"
        "end\n"
        "fun pop_u(xs: List<Int32 | Bool, 2>): Int32\n"
        "  return unwrap(xs.pop())\n"
        "end\n"
        "var n: Int32 = pop_u([1, true])\n";

    return run_generated(source, "2\n", "union list pop");
}


TestResult test_runtime_slice(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "fun first(s: Slice<Int32>): Int32\n"
        "  return s[0] + s.len()\n"
        "end\n"
        "fun make(xs: List<Int32>): Int32\n"
        "  xs.push(10)\n"
        "  xs.push(20)\n"
        "  xs.push(30)\n"
        "  return first(xs.slice(0, 3).slice(1, 2))\n"
        "end\n"
        "var n: Int32 = make(arena.list<Int32>(2))\n";

    return run_generated(source, "22\n", "list slice");
}

TestResult test_runtime_slice_stale(void)
{
    const char *source =
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(1)\n"
        "var s: Slice<Int32> = xs.slice(0, 1)\n"
        "arena.reset()\n"
        "var n: Int32 = s[0]\n";

    return run_generated_failure(source, MESSAGE_RUNTIME_STALE_HANDLE,
                                 "stale slice");
}

/* Every test in this suite, in run order. The table lives here so adding
   a test needs only its definition above and one entry below. */
static const TestCase tests[] = {
    {"runtime: program prints its value", test_runtime_program_prints_value},
    {"runtime: union narrowing", test_runtime_union_narrowing},
    {"runtime: generic instantiations", test_runtime_generic_instantiations},
    {"runtime: store alloc/read", test_runtime_store_alloc_read},
    {"runtime: store write", test_runtime_store_write},
    {"runtime: store byte", test_runtime_store_byte},
    {"runtime: store growth", test_runtime_store_growth},
    {"runtime: stale handle", test_runtime_stale_handle},
    {"runtime: two stores", test_runtime_two_stores},
    {"runtime: string ops", test_runtime_string_ops},
    {"runtime: string bounds", test_runtime_string_bounds},
    {"runtime: string union", test_runtime_string_union},
    {"runtime: Error fields", test_runtime_error_fields},
    {"runtime: Error in store", test_runtime_error_in_store},
    {"runtime: String in store", test_runtime_string_in_store},
    {"runtime: global store", test_runtime_global_store},
    {"runtime: dynamic strings", test_runtime_dynamic_strings},
    {"runtime: dynamic String handle", test_runtime_dynamic_string_in_handle},
    {"runtime: stale dynamic string", test_runtime_dynamic_string_stale},
    {"runtime: dynamic string bounds", test_runtime_dynamic_string_bounds},
    {"runtime: dynamic String union", test_runtime_dynamic_string_union},
    {"runtime: StringBuilder", test_runtime_string_builder},
    {"runtime: StringBuilder content", test_runtime_string_builder_content},
    {"runtime: StringBuilder reuse", test_runtime_string_builder_reuse},
    {"runtime: fixed list", test_runtime_fixed_list},
    {"runtime: zero-length fixed list", test_runtime_fixed_list_zero},
    {"runtime: fixed list of strings", test_runtime_fixed_list_strings},
    {"runtime: dynamic list", test_runtime_dynamic_list},
    {"runtime: two dynamic list types", test_runtime_two_dynamic_lists},
    {"runtime: inferred list literal", test_runtime_inferred_list},
    {"runtime: fixed list mutation", test_runtime_fixed_list_mutation},
    {"runtime: dynamic list mutation", test_runtime_dynamic_list_mutation},
    {"runtime: list pop value", test_runtime_list_pop_value},
    {"runtime: list unions/functions", test_runtime_list_union_and_function},
    {"runtime: function list pop", test_runtime_function_list_pop},
    {"runtime: union list pop", test_runtime_union_list_pop},
    {"runtime: list slice", test_runtime_slice},
    {"runtime: stale slice", test_runtime_slice_stale},
    {"runtime: empty list pop", test_runtime_list_pop_empty},
    {"runtime: fixed list capacity", test_runtime_fixed_list_full},
    {"runtime: list bounds", test_runtime_list_bounds},
    {"runtime: stale list", test_runtime_list_stale},
    {"runtime: stale StringBuilder", test_runtime_string_builder_stale},
    {"runtime: stale dynamic string length", test_runtime_dynamic_string_stale_len},
    {"runtime: top-level assignment", test_runtime_assignment_top},
    {"runtime: parameter assignment", test_runtime_assignment_parameter},
    {"runtime: string assignment", test_runtime_assignment_string},
    {"runtime: assignment narrowing", test_runtime_assignment_union_narrowing},
    {"runtime: byte assignment", test_runtime_assignment_byte},
    {"runtime: Error assignment", test_runtime_assignment_error},
    {"runtime: handle assignment", test_runtime_assignment_handle},
    {"runtime: assignment in a loop", test_runtime_assignment_loop},
    {"runtime: repeated assignment", test_runtime_assignment_repeated},
    {"runtime: generic assignment", test_runtime_assignment_generic},
    {"runtime: inferred assignment", test_runtime_assignment_inferred},
    {"runtime: nested narrowing invalidation",
     test_runtime_assignment_nested_narrowing},
    {"runtime: union assignment", test_runtime_assignment_union},
    {"driver: could not open source", test_driver_could_not_open_source},
    {"driver: source too large", test_driver_source_too_large},
    {"driver: source contains NUL", test_driver_source_contains_nul},
    {"driver: failed to read source", test_driver_failed_to_read},
    {"driver: usage", test_driver_usage},
    {"driver: could not write header", test_driver_could_not_write_header},
    {"allocation failures fail closed", test_allocation_failures_fail_closed},
    {"allocation failure messages", test_allocation_failure_messages},
    {"driver: could not write source", test_driver_could_not_write_source},
    {"driver: C compiler failures", test_driver_cc_failures},
};

int main(void)
{
    return test_run_suite("tool", tests,
                          sizeof(tests) / sizeof(tests[0]));
}
