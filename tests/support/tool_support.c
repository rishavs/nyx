/* fork, execv, and setenv are POSIX, not C11. */
#define _POSIX_C_SOURCE 200809L

#include "tool_support.h"

#include <fcntl.h>


int write_text(const char *path, const char *text)
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


int write_bytes(const char *path, const unsigned char *data, size_t length)
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


int file_has_content(const char *path)
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


int file_contains(const char *path, const char *needle)
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


int file_equals(const char *path, const char *expected)
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


const char *nippy_binary(void)
{
    const char *override = getenv("NIPPY_BIN");

    return override != NULL && override[0] != '\0' ? override : "./bin/nippy";
}


int run_compiler(const char *source_path, const char *binary, const char *log)
{
    char command[1536];

    snprintf(command, sizeof(command), "%s %s %s > %s 2>&1", nippy_binary(),
             source_path, binary, log);
    return system(command);
}


/* Allocation-failure sweeps start the compiler hundreds of times, so this
   starts it directly instead of through a shell. The result is a wait status,
   like system()'s. */
int run_with_alloc_failure(size_t index, const char *source_path,
                           const char *binary, const char *log)
{
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        char value[32];
        int descriptor = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (descriptor < 0) {
            _exit(126);
        }
        dup2(descriptor, STDOUT_FILENO);
        dup2(descriptor, STDERR_FILENO);
        close(descriptor);
        snprintf(value, sizeof(value), "%zu", index);
        setenv("NIPPY_FAIL_ALLOC_AFTER", value, 1);
        execl(nippy_binary(), nippy_binary(), source_path, binary, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return status;
}


int run_command(const char *command, const char *log)
{
    size_t size = strlen(command) + strlen(log) + 16;
    char *buffer = malloc(size);
    int status;

    if (buffer == NULL) {
        return -1;
    }
    snprintf(buffer, size, "%s > %s 2>&1", command, log);
    status = system(buffer);
    free(buffer);
    return status;
}


/* How a generated program is judged once it has run. */
typedef enum {
    EXPECT_OUTPUT,
    EXPECT_FAILURE
} Expectation;


/* Compiles `source`, builds the generated C (optionally with a separate
   driver), runs it, and checks the outcome. This is the one place that knows
   how a generated program is built, so the public helpers below stay thin. */
static TestResult build_and_run(const char *source, const char *driver,
                                Expectation expectation, const char *expected,
                                const char *what)
{
    CompilerConfig config = {.header_name = "runtime.h",
                             .source_name = "test.nip"};
    CompilerResult compiled = compiler_compile(source, &config);
    TestResult result = TEST_ERROR;
    char header[1024];
    char program[1024];
    char log[1024];
    char command[4096];
    int status;

    snprintf(header, sizeof(header), "%s", test_path("runtime.h"));
    snprintf(program, sizeof(program), "%s", test_path("runtime"));
    snprintf(log, sizeof(log), "%s", test_path("runtime.log"));
    if (!compiled.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                compiled.error_message == NULL ? "(no diagnostic)"
                                               : compiled.error_message);
        goto finished;
    }
    if (!write_text(header, compiled.h_source) ||
        !write_text(test_path("runtime.c"), compiled.c_source)) {
        fprintf(stderr, "  %s: could not write the generated C\n", what);
        goto finished;
    }
    if (driver == NULL) {
        snprintf(command, sizeof(command), "cc -std=c11 -o %s %s", program,
                 test_path("runtime.c"));
        status = run_command(command, log);
    } else {
        char object[1024];
        char driver_object[1024];

        snprintf(object, sizeof(object), "%s", test_path("runtime.o"));
        snprintf(driver_object, sizeof(driver_object), "%s",
                 test_path("runtime-main.o"));
        if (!write_text(test_path("runtime-main.c"), driver)) {
            fprintf(stderr, "  %s: could not write the driver\n", what);
            goto finished;
        }
        /* The generated translation unit always defines main; rename it so
           the driver's main can call the Nippy functions. */
        snprintf(command, sizeof(command),
                 "cc -std=c11 -Dmain=nyx_generated_main -c %s -o %s",
                 test_path("runtime.c"), object);
        status = run_command(command, log);
        if (status == 0) {
            snprintf(command, sizeof(command), "cc -std=c11 -c %s -o %s",
                     test_path("runtime-main.c"), driver_object);
            status = run_command(command, log);
        }
        if (status == 0) {
            snprintf(command, sizeof(command), "cc -o %s %s %s", program,
                     object, driver_object);
            status = run_command(command, log);
        }
    }
    if (status != 0) {
        fprintf(stderr, "  %s: generated C did not build\n", what);
        goto finished;
    }
    status = run_command(program, log);
    if (expectation == EXPECT_OUTPUT) {
        if (status != 0) {
            fprintf(stderr, "  %s: program exited non-zero\n", what);
            goto finished;
        }
        if (!file_equals(log, expected)) {
            fprintf(stderr, "  %s: output did not match \"%s\"\n", what,
                    expected);
            goto finished;
        }
    } else {
        if (status == 0) {
            fprintf(stderr, "  %s: program unexpectedly succeeded\n", what);
            goto finished;
        }
        if (!file_contains(log, expected)) {
            fprintf(stderr, "  %s: missing runtime diagnostic\n", what);
            goto finished;
        }
    }
    result = TEST_PASS;

finished:
    compiler_result_free(&compiled);
    return result;
}


TestResult run_generated(const char *source, const char *expected,
                         const char *what)
{
    return build_and_run(source, NULL, EXPECT_OUTPUT, expected, what);
}


TestResult run_generated_driver(const char *source, const char *driver,
                                const char *expected, const char *what)
{
    return build_and_run(source, driver, EXPECT_OUTPUT, expected, what);
}


TestResult run_generated_failure(const char *source, const char *log_needle,
                                 const char *what)
{
    return build_and_run(source, NULL, EXPECT_FAILURE, log_needle, what);
}
