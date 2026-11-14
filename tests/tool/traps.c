/* Run-time traps provoked directly in the generated helpers. */

#include "tool_support.h"

/* Builds the generated program together with a main that runs `main_body`, so
   a trap can be provoked directly in the runtime helpers, and expects the
   diagnostic `expected`. */
static TestResult run_generated_trap(const char *main_body,
                                     const char *expected,
                                     const char *what)
{
    const char *source =
        "global arena: Store = Store(16)\n"
        "var value: Int32 = 1\n";
    CompilerConfig config = {.header_name = "runtime.h",
                             .source_name = "test.nip"};
    CompilerResult compiled = compiler_compile(source, &config);
    TestResult result = TEST_ERROR;
    char program[1024];
    char log[1024];
    char command[4096];
    int status;

    snprintf(program, sizeof(program), "%s", test_path("trap"));
    snprintf(log, sizeof(log), "%s", test_path("trap.log"));
    if (!compiled.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                compiled.error_message == NULL ? "(no diagnostic)"
                                               : compiled.error_message);
        goto finished;
    }
    if (!write_text(test_path("runtime.h"), compiled.h_source) ||
        !write_text(test_path("runtime.c"), compiled.c_source)) {
        fprintf(stderr, "  %s: could not write the generated C\n", what);
        goto finished;
    }
    {
        char driver[2048];

        snprintf(driver, sizeof(driver),
                 "#define main nyx_generated_main\n"
                 "#include \"runtime.c\"\n"
                 "#undef main\n"
                 "\n"
                 "int main(void)\n"
                 "{\n"
                 "%s\n"
                 "    return 0;\n"
                 "}\n",
                 main_body);
        if (!write_text(test_path("trap.c"), driver)) {
            fprintf(stderr, "  %s: could not write the trap driver\n", what);
            goto finished;
        }
    }
    snprintf(command, sizeof(command),
             "cc -std=c11 -Wall -Wextra -Wpedantic -o %s %s", program,
             test_path("trap.c"));
    status = run_command(command, log);
    if (status != 0) {
        fprintf(stderr, "  %s: trap driver did not build\n", what);
        goto finished;
    }
    status = run_command(program, log);
    if (status == -1 || WIFSIGNALED(status) || status == 0) {
        fprintf(stderr, "  %s: trap did not exit with a diagnostic\n", what);
        goto finished;
    }
    if (!file_contains(log, expected)) {
        fprintf(stderr, "  %s: expected \"%s\"\n", what, expected);
        goto finished;
    }
    result = TEST_PASS;

finished:
    compiler_result_free(&compiled);
    return result;
}

TEST("store range trap")
{
    return run_generated_trap(
        "    nyx_handle handle = {0};\n"
        "    handle.store = 9999;\n"
        "    handle.generation = 1;\n"
        "    (void)nyx_handle_pointer(handle, sizeof(int32_t));\n",
        MESSAGE_RUNTIME_STORE_RANGE, "store range trap");
}

TEST("handle bounds trap")
{
    return run_generated_trap(
        "    nyx_handle handle = {0};\n"
        "    handle.store = 0;\n"
        "    handle.offset = 999999;\n"
        "    handle.generation = nyx_stores[0].generation;\n"
        "    (void)nyx_handle_pointer(handle, sizeof(int32_t));\n",
        MESSAGE_RUNTIME_HANDLE_BOUNDS, "handle bounds trap");
}

TEST("store OOM trap")
{
    return run_generated_trap(
        "    nyx_store store = {0};\n"
        "    store.data = malloc(1);\n"
        "    store.capacity = 1;\n"
        "    nyx_store_reserve(&store, (size_t)-1);\n"
        "    return 0;\n",
        MESSAGE_RUNTIME_OUT_OF_MEMORY, "store OOM trap");
}

TEST("store OOM trap includes its location")
{
    return run_generated_trap(
        "    nyx_store store = {0};\n"
        "    store.data = malloc(1);\n"
        "    store.capacity = 1;\n"
        "    nyx_store_reserve_at(&store, (size_t)-1, \"source.nip\", 2, 3, 4);\n"
        "    return 0;\n",
        "nippy: source.nip:2:3 (byte 4):", "positioned store OOM trap");
}
