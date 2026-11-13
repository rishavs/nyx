#include "tool_support.h"

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
    "tests/.test-runtime-main.o",
    "tests/.test-trap",
    "tests/.test-trap.c",
    "tests/.test-trap.log"
};


void cleanup(void)
{
    size_t i;
    for (i = 0; i < sizeof(temporary_files) / sizeof(temporary_files[0]); i++) {
        remove(temporary_files[i]);
    }
}


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

int run_compiler(const char *source_path, const char *binary,
                        const char *log)
{
    char command[512];

    snprintf(command, sizeof(command), "./bin/nippy %s %s > %s 2>&1",
             source_path, binary, log);
    return system(command);
}


int run_command(const char *command, const char *log)
{
    char buffer[512];

    snprintf(buffer, sizeof(buffer), "%s > %s 2>&1", command, log);
    return system(buffer);
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

TestResult run_generated(const char *source, const char *expected,
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

TestResult run_generated_driver(const char *source, const char *driver,
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
        "cc -std=c11 -Dmain=nyx_generated_main -c tests/.test-runtime.c "
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

int run_with_alloc_failure(size_t index, const char *source_path,
                                  const char *binary, const char *log)
{
    char command[640];

    snprintf(command, sizeof(command),
             "NIPPY_FAIL_ALLOC_AFTER=%zu ./bin/nippy %s %s > %s 2>&1",
             index, source_path, binary, log);
    return system(command);
}

TestResult run_generated_failure(const char *source,
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

