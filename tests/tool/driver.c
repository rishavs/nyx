/* The nippy command line: file I/O, the cc invocation, and allocation failure. */

#include "tool_support.h"

#include <sys/stat.h>

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
    "  if typeof x is Int32 then\n"
    "    return x\n"
    "  elseif typeof x is Bool then\n"
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
    "var s: Int32 = identity(Int32(7))\n"
    "var t: Int32 = classify(true)\n"
    "var u: Int32 = add(1, 2)\n"
    "var arena: Store = Store(16)\n"
    "var stored: Handle<Int32> = arena.alloc<Int32>(1)\n"
    "var byte_value: Byte = 7\n";

TEST("paths with spaces")
{
    char source[1024];
    char output[1024];
    char log[1024];
    char command[4096];

    snprintf(source, sizeof(source), "%s", test_path("source file.nip"));
    snprintf(output, sizeof(output), "%s", test_path("binary file"));
    snprintf(log, sizeof(log), "%s", test_path("compiler.log"));
    if (!write_text(source, "print(3)\n")) return TEST_ERROR;
    snprintf(command, sizeof(command), "%s '%s' '%s' > '%s' 2>&1",
             nippy_binary(), source, output, log);
    if (system(command) != 0) {
        fprintf(stderr, "  source or output path with spaces failed\n");
        return TEST_FAIL;
    }
    snprintf(command, sizeof(command), "'%s'", output);
    if (run_command(command, log) != 0 || !file_contains(log, "3\n")) {
        fprintf(stderr, "  output with spaces did not run\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("could not open source")
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    status = run_compiler("/nonexistent/nope.nip", test_path("bin"),
                          test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  a nonexistent source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_COULD_NOT_OPEN_SOURCE, "/nonexistent/nope.nip");
    if (!file_has_content(test_path("log")) ||
        !file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing \"could not open source file\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("source too large")
{
    char big[4097];
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    memset(big, 'a', sizeof(big));
    if (!write_bytes(test_path("source.nip"), (const unsigned char *)big,
                     sizeof(big))) {
        fprintf(stderr, "  could not write the oversized source\n");
        goto finished;
    }
    status = run_compiler(test_path("source.nip"), test_path("bin"), test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  an oversized source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_SOURCE_TOO_LARGE, test_path("source.nip"), 4096);
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing \"source is larger than\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("source limit can be raised")
{
    unsigned char source[5000];
    const char suffix[] = "\nvar x: Int32 = 1\n";
    char command[2048];
    TestResult result = TEST_ERROR;
    int status;

    source[0] = '-';
    source[1] = '-';
    memset(source + 2, 'x', sizeof(source) - 2 - sizeof(suffix) + 1);
    memcpy(source + sizeof(source) - sizeof(suffix) + 1, suffix,
           sizeof(suffix) - 1);
    if (!write_bytes(test_path("source.nip"), source, sizeof(source))) {
        fprintf(stderr, "  could not write the large source\n");
        goto finished;
    }
    snprintf(command, sizeof(command),
             "%s --max-source-bytes 8192 %s %s > %s 2>&1",
             nippy_binary(), test_path("source.nip"), test_path("bin"),
             test_path("log"));
    status = system(command);
    if (status != 0) {
        fprintf(stderr, "  raised source limit did not compile the source\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("invalid source limit")
{
    char command[2048];
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    snprintf(command, sizeof(command),
             "%s --max-source-bytes nope source.nip bin/test > %s 2>&1",
             nippy_binary(), test_path("log"));
    status = system(command);
    if (status == 0) {
        fprintf(stderr, "  invalid source limit was accepted\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected), MESSAGE_DRIVER_SOURCE_LIMIT);
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing invalid source limit diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("source contains NUL")
{
    static const unsigned char data[] = {
        'v', 'a', 'r', ' ', 'x', ' ', ':', ' ', 'I', 'n', 't', '3', '2',
        ' ', '=', ' ', '1', '\0', '\n'
    };
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    if (!write_bytes(test_path("source.nip"), data, sizeof(data))) {
        fprintf(stderr, "  could not write the NUL-containing source\n");
        goto finished;
    }
    status = run_compiler(test_path("source.nip"), test_path("bin"), test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  a NUL-containing source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_SOURCE_CONTAINS_NUL, test_path("source.nip"));
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing \"source contains a NUL byte\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("failed to read source")
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    status = run_compiler("tests", test_path("bin"), test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  a directory source unexpectedly compiled\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected), MESSAGE_DRIVER_FAILED_TO_READ,
                   "tests");
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing \"failed to read source file\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("usage")
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    status = run_command(nippy_binary(), test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  no-argument invocation unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected), MESSAGE_DRIVER_USAGE,
                   nippy_binary());
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing usage diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("could not write header")
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    if (!write_text(test_path("source.nip"), "var x : Int32 = 13\n")) {
        fprintf(stderr, "  could not write the source\n");
        goto finished;
    }
    status = run_compiler(test_path("source.nip"), "/nonexistent-dir/out",
                          test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  an unwritable output path unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_COULD_NOT_WRITE_HEADER, "/nonexistent-dir/out.h");
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing \"could not write generated header\" diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("allocation failures fail closed")
{
    size_t index;
    TestResult result = TEST_ERROR;

    if (!write_text(test_path("source.nip"), oom_source)) {
        fprintf(stderr, "  could not write the OOM source\n");
        goto finished;
    }
    for (index = 0; index < 400; index++) {
        int status;

        remove(test_path("bin"));
        status = run_with_alloc_failure(index, test_path("source.nip"),
                                        test_path("bin"), test_path("log"));

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
        if (WEXITSTATUS(status) != 0 && !file_has_content(test_path("log"))) {
            fprintf(stderr, "  no diagnostic at allocation index %zu\n", index);
            result = TEST_FAIL;
            goto finished;
        }
        if (WEXITSTATUS(status) != 0 && access(test_path("bin"), F_OK) == 0) {
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
    return result;
}

TEST("allocation failure messages")
{
    char expected[10][256];
    int seen[10] = {0};
    size_t expected_count = 0;
    size_t remaining;
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

    remaining = expected_count;
    /* The sweep below reaches a message by failing the nth allocation. The
       poisoned build allocates on every list push, so the compiler makes tens
       of thousands of allocations and the messages sit far beyond the sweep's
       range: the test measures something the poisoned build changes. */
    if (getenv("NIPPY_POISON_BUILD") != NULL) {
        fprintf(stderr, "  skipped: allocation counts differ in the poisoned "
                        "build\n");
        return TEST_TODO;
    }
    if (!write_text(test_path("source.nip"), oom_source)) {
        fprintf(stderr, "  could not write the OOM source\n");
        goto finished;
    }

    /* One pass over the failure index: each run may produce any of the
       messages, and the sweep ends as soon as every message has been seen. */
    for (index = 0; index < 400 && remaining > 0; index++) {
        int status = run_with_alloc_failure(index, test_path("source.nip"),
                                            test_path("bin"), test_path("log"));

        if (status == -1 || WIFSIGNALED(status)) {
            fprintf(stderr, "  compiler failed to run at index %zu\n", index);
            goto finished;
        }
        for (i = 0; i < expected_count; i++) {
            if (!seen[i] && file_contains(test_path("log"), expected[i])) {
                seen[i] = 1;
                remaining--;
            }
        }
    }
    for (i = 0; i < expected_count; i++) {
        if (!seen[i]) {
            fprintf(stderr, "  never produced \"%s\"\n", expected[i]);
            result = TEST_FAIL;
            goto finished;
        }
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("could not write source")
{
    char expected[256];
    TestResult result = TEST_ERROR;
    int status;

    if (!write_text(test_path("source.nip"), "var x : Int32 = 13\n")) {
        fprintf(stderr, "  could not write the source\n");
        goto finished;
    }
    /* A directory occupying the .c path lets the header write succeed and the
       C write fail. */
    if (mkdir(test_path("bin.c"), 0700) != 0) {
        fprintf(stderr, "  could not create the blocking directory\n");
        goto finished;
    }
    status = run_compiler(test_path("source.nip"), test_path("bin"), test_path("log"));
    if (status == 0) {
        fprintf(stderr, "  an unwritable C path unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    expect_message(expected, sizeof(expected),
                   MESSAGE_DRIVER_COULD_NOT_WRITE_SOURCE, test_path("bin.c"));
    if (!file_contains(test_path("log"), expected)) {
        fprintf(stderr, "  missing \"could not write generated C source\"\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}

TEST("C compiler failures")
{
    TestResult result = TEST_ERROR;
    int status;

    if (!write_text(test_path("source.nip"), "var x : Int32 = 13\n")) {
        fprintf(stderr, "  could not write the source\n");
        goto finished;
    }
    /* An empty PATH leaves `cc` unfindable, so system() reports a failure to
       start the shell command. */
    {
        char command[2048];

        snprintf(command, sizeof(command),
                 "PATH=/nonexistent %s %s %s > %s 2>&1", nippy_binary(),
                 test_path("source.nip"), test_path("bin"), test_path("log"));
        status = system(command);
    }
    if (status == 0) {
        fprintf(stderr, "  a missing C compiler unexpectedly succeeded\n");
        result = TEST_FAIL;
        goto finished;
    }
    if (!file_contains(test_path("log"), MESSAGE_DRIVER_COULD_NOT_START_CC) &&
        !file_contains(test_path("log"), MESSAGE_DRIVER_CC_FAILED)) {
        fprintf(stderr, "  missing C compiler failure diagnostic\n");
        result = TEST_FAIL;
        goto finished;
    }
    result = TEST_PASS;

finished:
    return result;
}
