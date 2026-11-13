/* Golden tests: each tests/compiler/golden/NAME.nip is compiled, and the C
   source and header it generates must equal NAME.c and NAME.h exactly. These
   pin the generator's output, so they change whenever generated C does.

   To add a test, write NAME.nip and run `make golden-update`. After a
   deliberate generator change, run `make golden-update` and review the diff:
   it rewrites the expected files from the compiler's current output. Tests are
   found when the runner starts, so run it from the repository root. */

/* opendir and readdir are POSIX, not C11. */
#define _POSIX_C_SOURCE 200809L

#include "compile_support.h"

#include <dirent.h>

#define GOLDEN_DIRECTORY "tests/compiler/golden"


static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    char *text;
    long length;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = malloc((size_t)length + 1);
    if (text != NULL) {
        size_t got = fread(text, 1, (size_t)length, file);

        text[got] = '\0';
    }
    fclose(file);
    return text;
}


static int write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    int ok;

    if (file == NULL) {
        perror(path);
        return 0;
    }
    ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}


static int update_file(const char *stem, const char *extension,
                       const char *text)
{
    char path[512];
    char *current;
    int same;

    snprintf(path, sizeof(path), "%s.%s", stem, extension);
    current = read_file(path);
    same = current != NULL && strcmp(current, text) == 0;
    free(current);
    if (same) {
        return 1;
    }
    if (!write_file(path, text)) {
        return 0;
    }
    fprintf(stderr, "  updated %s\n", path);
    return 1;
}


static TestResult update_golden(const char *stem, const char *source)
{
    CompilerResult result = compiler_compile(source, &test_config);
    int ok;

    if (!result.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", stem,
                result.error_message == NULL ? "(no diagnostic)"
                                             : result.error_message);
        compiler_result_free(&result);
        return TEST_FAIL;
    }
    /* Only files whose content changes are rewritten and mentioned, so the
       output of an update lists exactly what to review. */
    ok = update_file(stem, "c", result.c_source);
    ok = update_file(stem, "h", result.h_source) && ok;
    compiler_result_free(&result);
    return ok ? TEST_PASS : TEST_ERROR;
}


static TestResult run_golden(void *data)
{
    const char *stem = data;
    char path[512];
    char *source;
    char *expected_c;
    char *expected_h;
    TestResult result = TEST_ERROR;
    const char *update = getenv("NIPPY_UPDATE_GOLDEN");

    snprintf(path, sizeof(path), "%s.nip", stem);
    source = read_file(path);
    if (source == NULL) {
        fprintf(stderr, "  could not read %s\n", path);
        return TEST_ERROR;
    }
    if (update != NULL && update[0] != '\0') {
        result = update_golden(stem, source);
        free(source);
        return result;
    }
    snprintf(path, sizeof(path), "%s.c", stem);
    expected_c = read_file(path);
    snprintf(path, sizeof(path), "%s.h", stem);
    expected_h = read_file(path);
    if (expected_c == NULL || expected_h == NULL) {
        fprintf(stderr, "  %s has no expected output; run make golden-update\n",
                stem);
    } else {
        result = compile_succeeds(source, expected_c, expected_h, stem);
        if (result != TEST_PASS) {
            fprintf(stderr, "  to accept the new output: make golden-update\n");
        }
    }
    free(source);
    free(expected_c);
    free(expected_h);
    return result;
}


static TestResult golden_directory_missing(void *data)
{
    (void)data;
    fprintf(stderr, "  cannot read %s; run the tests from the repository "
                    "root\n", GOLDEN_DIRECTORY);
    return TEST_ERROR;
}


static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}


static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}


/* Registers one test per .nip file, named by the file with '-' read as a
   space. */
static void register_golden_tests(void) __attribute__((constructor));
static void register_golden_tests(void)
{
    DIR *directory = opendir(GOLDEN_DIRECTORY);
    struct dirent *entry;
    char **stems = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t i;

    if (directory == NULL) {
        test_register_data(__FILE__, __LINE__, "golden directory",
                           golden_directory_missing, NULL);
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);

        if (length > 4 && strcmp(entry->d_name + length - 4, ".nip") == 0) {
            if (count == capacity) {
                size_t grown = capacity == 0 ? 64 : capacity * 2;
                char **resized = realloc(stems, grown * sizeof(char *));

                if (resized == NULL) {
                    break;
                }
                stems = resized;
                capacity = grown;
            }
            stems[count] = copy_text(entry->d_name);
            if (stems[count] == NULL) {
                break;
            }
            stems[count][length - 4] = '\0';
            count++;
        }
    }
    closedir(directory);
    qsort(stems, count, sizeof(char *), compare_names);
    for (i = 0; i < count; i++) {
        char *name = copy_text(stems[i]);
        char *path = malloc(strlen(GOLDEN_DIRECTORY) + strlen(stems[i]) + 2);
        char *c;

        if (name == NULL || path == NULL) {
            break;
        }
        sprintf(path, "%s/%s", GOLDEN_DIRECTORY, stems[i]);
        for (c = name; *c != '\0'; c++) {
            if (*c == '-') {
                *c = ' ';
            }
        }
        test_register_data(__FILE__, __LINE__, name, run_golden, path);
        free(stems[i]);
    }
    free(stems);
}
