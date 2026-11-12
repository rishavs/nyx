#include "tool_support.h"

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
    "var s: Int32 = identity(7)\n"
    "var t: Int32 = classify(true)\n"
    "var u: Int32 = add(1, 2)\n"
    "var arena: Store = Store(16)\n"
    "var stored: Handle<Int32> = arena.alloc<Int32>(1)\n"
    "var byte_value: Byte = 7\n";


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

const TestCase driver_test_tests[] = {
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
const size_t driver_test_test_count =
    sizeof(driver_test_tests) / sizeof(driver_test_tests[0]);
