#include "tool_support.h"


static TestResult run_generated_trap(const char *main_body,
                                     const char *expected,
                                     const char *what)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var value: Int32 = 1\n";
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
    {
        char driver[2048];

        snprintf(driver, sizeof(driver),
                 "#define main nyx_generated_main\n"
                 "#include \".test-runtime.c\"\n"
                 "#undef main\n"
                 "\n"
                 "int main(void)\n"
                 "{\n"
                 "%s\n"
                 "    return 0;\n"
                 "}\n",
                 main_body);
        if (!write_text("tests/.test-trap.c", driver)) {
            fprintf(stderr, "  %s: could not write the trap driver\n", what);
            goto finished;
        }
    }
    status = run_command(
        "cc -std=c11 -Wall -Wextra -Wpedantic -I tests "
        "-o tests/.test-trap tests/.test-trap.c",
        "tests/.test-trap.log");
    if (status != 0) {
        fprintf(stderr, "  %s: trap driver did not build\n", what);
        goto finished;
    }
    status = run_command("tests/.test-trap", "tests/.test-trap.log");
    if (status == -1 || WIFSIGNALED(status) || status == 0) {
        fprintf(stderr, "  %s: trap did not exit with a diagnostic\n", what);
        goto finished;
    }
    if (!file_contains("tests/.test-trap.log", expected)) {
        fprintf(stderr, "  %s: expected \"%s\"\n", what, expected);
        goto finished;
    }
    result = TEST_PASS;

finished:
    compiler_result_free(&compiled);
    cleanup();
    return result;
}


TestResult test_runtime_store_range_trap(void)
{
    return run_generated_trap(
        "    nyx_handle handle = {0};\n"
        "    handle.store = 9999;\n"
        "    handle.generation = 1;\n"
        "    (void)nyx_handle_pointer(handle, sizeof(int32_t));\n",
        MESSAGE_RUNTIME_STORE_RANGE, "store range trap");
}


TestResult test_runtime_handle_bounds_trap(void)
{
    return run_generated_trap(
        "    nyx_handle handle = {0};\n"
        "    handle.store = 0;\n"
        "    handle.offset = 999999;\n"
        "    handle.generation = nyx_stores[0].generation;\n"
        "    (void)nyx_handle_pointer(handle, sizeof(int32_t));\n",
        MESSAGE_RUNTIME_HANDLE_BOUNDS, "handle bounds trap");
}


TestResult test_runtime_store_oom_trap(void)
{
    return run_generated_trap(
        "    nyx_store store = {0};\n"
        "    store.data = malloc(1);\n"
        "    store.capacity = 1;\n"
        "    nyx_store_reserve(&store, (size_t)-1);\n"
        "    return 0;\n",
        MESSAGE_RUNTIME_OUT_OF_MEMORY, "store OOM trap");
}

const TestCase runtime_trap_test_tests[] = {
    {"runtime: store range trap", test_runtime_store_range_trap},
    {"runtime: handle bounds trap", test_runtime_handle_bounds_trap},
    {"runtime: store OOM trap", test_runtime_store_oom_trap},
};
const size_t runtime_trap_test_test_count =
    sizeof(runtime_trap_test_tests) / sizeof(runtime_trap_test_tests[0]);
