#include "test_harness.h"
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

/* Every test in this suite, in run order. The table lives here so adding
   a test needs only its definition above and one entry below. */
static const TestCase tests[] = {
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
