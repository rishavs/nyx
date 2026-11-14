#define _POSIX_C_SOURCE 200809L

#include "alloc.h"
#include "compiler.h"
#include "error.h"
#include "messages.h"

#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* True when both paths name the same existing file, however they are spelled
   (`x.nip`, `./x.nip`, a symlink). A path that does not exist yet cannot be the
   source, so it is a different file. */
static int is_same_file(const char *first, const char *second)
{
    struct stat first_status;
    struct stat second_status;

    if (strcmp(first, second) == 0) {
        return 1;
    }
    if (stat(first, &first_status) != 0 || stat(second, &second_status) != 0) {
        return 0;
    }
    return first_status.st_dev == second_status.st_dev &&
           first_status.st_ino == second_status.st_ino;
}

static void print_errors(ErrorState *errors)
{
    error_unknown_if_none(errors);
    fputs(errors->message, stderr);
}

static char *read_source(const char *path, size_t max_source_size,
                         ErrorState *errors)
{
    FILE *file = fopen(path, "rb");
    char *source;
    size_t length;
    int next;

    if (file == NULL) {
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_OPEN_SOURCE, path);
        return NULL;
    }

    source = nippy_malloc(max_source_size + 1);
    if (source == NULL) {
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_BUFFER);
        fclose(file);
        return NULL;
    }
    length = fread(source, 1, max_source_size, file);
    if (ferror(file)) {
        error_report(errors, MESSAGE_DRIVER_FAILED_TO_READ, path);
        free(source);
        fclose(file);
        return NULL;
    }
    if (length == max_source_size) {
        next = fgetc(file);
        if (next != EOF) {
            error_report(errors, MESSAGE_DRIVER_SOURCE_TOO_LARGE, path,
                         max_source_size);
            free(source);
            fclose(file);
            return NULL;
        }
        if (ferror(file)) {
            error_report(errors, MESSAGE_DRIVER_FAILED_TO_READ, path);
            free(source);
            fclose(file);
            return NULL;
        }
    }
    /* A read-only stream's fclose can fail only after a failed read, which
       the ferror checks above already reported, so treat it the same way. */
    if (fclose(file) != 0) {
        error_report(errors, MESSAGE_DRIVER_FAILED_TO_READ, path);
        free(source);
        return NULL;
    }
    if (memchr(source, '\0', length) != NULL) {
        error_report(errors, MESSAGE_DRIVER_SOURCE_CONTAINS_NUL, path);
        free(source);
        return NULL;
    }

    source[length] = '\0';
    return source;
}

static char *append_suffix(const char *path, const char *suffix)
{
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    char *result;

    if (suffix_length == SIZE_MAX || path_length > SIZE_MAX - suffix_length - 1)
        return NULL;
    result = nippy_malloc(path_length + suffix_length + 1);

    if (result != NULL) {
        memcpy(result, path, path_length);
        memcpy(result + path_length, suffix, suffix_length + 1);
    }
    return result;
}

typedef struct {
    char *directory;
    char *binary;
    char *source;
    char *header;
    char *backups;
    bool created;
    bool backups_created;
    bool preserve;
} StagedOutput;

/* One-shot test hook for exercising publication rollback. */
static size_t rename_fail_after = NIPPY_ALLOC_NEVER;

static int publish_rename(const char *from, const char *to)
{
    if (rename_fail_after == 0) {
        rename_fail_after = NIPPY_ALLOC_NEVER;
        errno = EIO;
        return -1;
    }
    if (rename_fail_after != NIPPY_ALLOC_NEVER) rename_fail_after--;
    return rename(from, to);
}

static void free_staged_output(StagedOutput *stage)
{
    if (!stage->preserve) {
        if (stage->binary != NULL) unlink(stage->binary);
        if (stage->source != NULL) unlink(stage->source);
        if (stage->header != NULL) unlink(stage->header);
        if (stage->backups_created) rmdir(stage->backups);
        if (stage->created) rmdir(stage->directory);
    }
    free(stage->binary);
    free(stage->source);
    free(stage->header);
    free(stage->backups);
    free(stage->directory);
}

static bool create_staged_output(const char *output, const char *h_path,
                                 StagedOutput *stage,
                                 ErrorState *errors)
{
    const char *slash = strrchr(output, '/');
    const char *base = slash == NULL ? output : slash + 1;
    size_t dir_length = slash == NULL ? 1 : (size_t)(slash - output);
    const char *dir = slash == NULL ? "." : output;
    const char *backup_name = strcmp(base, ".saved") == 0 ? "/.saved2" : "/.saved";
    size_t size, base_length = strlen(base);

    if (slash != NULL && dir_length == 0) dir_length = 1;
    if (*base == '\0' || base_length > SIZE_MAX - 32 ||
        dir_length > SIZE_MAX - base_length - 32 || dir_length > INT_MAX) goto fail;
    size = dir_length + 24;
    stage->directory = nippy_malloc(size);
    if (stage->directory == NULL) goto oom;
    snprintf(stage->directory, size, "%.*s/.nippy-XXXXXX", (int)dir_length, dir);
    if (mkdtemp(stage->directory) == NULL) goto fail;
    stage->created = true;
    size = strlen(stage->directory) + strlen(base) + 2;
    stage->binary = nippy_malloc(size);
    if (stage->binary == NULL) goto oom;
    snprintf(stage->binary, size, "%s/%s", stage->directory, base);
    stage->source = append_suffix(stage->binary, ".c");
    stage->header = append_suffix(stage->binary, ".h");
    stage->backups = append_suffix(stage->directory, backup_name);
    if (stage->source == NULL || stage->header == NULL || stage->backups == NULL)
        goto oom;
    return true;
oom:
    error_report(errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_PATHS);
    return false;
fail:
    error_report(errors, MESSAGE_DRIVER_COULD_NOT_WRITE_HEADER, h_path);
    return false;
}

static bool publish_staged_output(StagedOutput *stage, const char *output,
                                  const char *c_path, const char *h_path,
                                  ErrorState *errors)
{
    const char *targets[3] = {h_path, c_path, output};
    const char *sources[3] = {stage->header, stage->source, stage->binary};
    char *saved[3] = {NULL, NULL, NULL};
    bool backed_up[3] = {false, false, false};
    size_t i, published = 0, failed = 0;
    bool ok = false, rollback_ok = true;
    struct stat status;

    for (i = 0; i < 3; i++) {
        if (lstat(targets[i], &status) == 0) {
            if (S_ISDIR(status.st_mode)) { failed = i; goto report; }
        } else if (errno != ENOENT) { failed = i; goto report; }
    }
    if (mkdir(stage->backups, 0700) != 0) goto report;
    stage->backups_created = true;
    for (i = 0; i < 3; i++) {
        char suffix[8];
        snprintf(suffix, sizeof(suffix), "/%zu", i);
        saved[i] = append_suffix(stage->backups, suffix);
        if (saved[i] == NULL) {
            error_report(errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_PATHS);
            goto rollback;
        }
        if (lstat(targets[i], &status) == 0) {
            if (publish_rename(targets[i], saved[i]) != 0) { failed = i; goto rollback; }
            backed_up[i] = true;
        } else if (errno != ENOENT) { failed = i; goto rollback; }
    }
    for (i = 0; i < 3; i++) {
        if (publish_rename(sources[i], targets[i]) != 0) { failed = i; goto rollback; }
        published++;
    }
    for (i = 0; i < 3; i++) if (backed_up[i]) unlink(saved[i]);
    ok = true;
    goto done;
rollback:
    for (i = 0; i < published; i++) if (unlink(targets[i]) != 0) rollback_ok = false;
    for (i = 0; i < 3; i++) {
        if (backed_up[i] && publish_rename(saved[i], targets[i]) != 0) rollback_ok = false;
    }
    if (!rollback_ok) {
        stage->preserve = true;
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_WRITE_SOURCE, stage->backups);
    }
report:
    if (!errors->reported) {
        if (failed == 0) error_report(errors, MESSAGE_DRIVER_COULD_NOT_WRITE_HEADER, h_path);
        else if (failed == 1) error_report(errors, MESSAGE_DRIVER_COULD_NOT_WRITE_SOURCE, c_path);
        else error_report(errors, MESSAGE_DRIVER_CC_FAILED);
    }
done:
    for (i = 0; i < 3; i++) free(saved[i]);
    return ok;
}

static int write_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    size_t length = strlen(contents);
    int ok;

    if (file == NULL) {
        return 0;
    }
    ok = fwrite(contents, 1, length, file) == length;
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

static char *shell_quote(const char *text)
{
    size_t length = strlen(text);
    char *quoted;
    char *out;

    if (length > (SIZE_MAX - 3) / 4) return NULL;
    quoted = nippy_malloc(length * 4 + 3);
    if (quoted == NULL) {
        return NULL;
    }
    out = quoted;
    *out++ = '\'';
    while (*text != '\0') {
        if (*text == '\'') {
            memcpy(out, "'\\''", 4);
            out += 4;
        } else {
            *out++ = *text;
        }
        text++;
    }
    *out++ = '\'';
    *out = '\0';
    return quoted;
}

static int build_binary(const char *c_path, const char *output_path,
                        ErrorState *errors)
{
    char *quoted_c = shell_quote(c_path);
    char *quoted_output = shell_quote(output_path);
    size_t command_size;
    char *command;
    int status;

    if (quoted_c == NULL || quoted_output == NULL) {
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_COMMAND);
        free(quoted_c);
        free(quoted_output);
        return 0;
    }
    if (strlen(quoted_output) > SIZE_MAX - 64 ||
        strlen(quoted_c) > SIZE_MAX - 64 - strlen(quoted_output)) {
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_COMMAND);
        free(quoted_c);
        free(quoted_output);
        return 0;
    }
    command_size = strlen(quoted_c) + strlen(quoted_output) + 64;
    command = nippy_malloc(command_size);
    if (command == NULL) {
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_COMMAND);
        free(quoted_c);
        free(quoted_output);
        return 0;
    }
    snprintf(command, command_size, "cc -std=c11 -o %s %s", quoted_output, quoted_c);
    status = system(command);
    free(command);
    free(quoted_c);
    free(quoted_output);

    if (status == -1) {
        error_report(errors, MESSAGE_DRIVER_COULD_NOT_START_CC);
        return 0;
    }
    if (status != 0) {
        error_report(errors, MESSAGE_DRIVER_CC_FAILED);
        return 0;
    }
    return 1;
}

/* Test hook: NIPPY_FAIL_ALLOC_AFTER makes the n-th allocation fail so the
   suite can exercise the driver's out-of-memory paths. Unset in normal use. */
static size_t failure_index_from_environment(const char *name)
{
    const char *value = getenv(name);

    if (value == NULL || *value == '\0') {
        return NIPPY_ALLOC_NEVER;
    }
    return (size_t)strtoul(value, NULL, 10);
}

static int parse_source_limit(const char *text, int *out_limit)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (*end != '\0' || value == 0 || value > INT_MAX) {
        return 0;
    }
    *out_limit = (int)value;
    return 1;
}

int main(int argc, char **argv)
{
    ErrorState errors;
    CompilerConfig config = {0};
    CompilerResult compiled;
    char *source;
    char *c_path;
    char *h_path;
    const char *header_name;
    const char *slash;
    const char *source_path;
    const char *output_path;
    int succeeded = 0;
    StagedOutput stage = {0};

    error_init(&errors);
    nippy_alloc_fail_after(failure_index_from_environment("NIPPY_FAIL_ALLOC_AFTER"));
    rename_fail_after = failure_index_from_environment("NIPPY_FAIL_RENAME_AFTER");
    source_path = NULL;
    output_path = NULL;
    if (argc == 3) {
        source_path = argv[1];
        output_path = argv[2];
    } else if (argc == 5 && strcmp(argv[1], "--max-source-bytes") == 0) {
        if (!parse_source_limit(argv[2], &config.max_source_bytes)) {
            error_report(&errors, MESSAGE_DRIVER_SOURCE_LIMIT);
            print_errors(&errors);
            return 2;
        }
        source_path = argv[3];
        output_path = argv[4];
    } else {
        error_report(&errors, MESSAGE_DRIVER_USAGE, argv[0]);
        print_errors(&errors);
        return 2;
    }
    errors.source_name = source_path;

    source = read_source(
        source_path,
        config.max_source_bytes > 0
            ? (size_t)config.max_source_bytes
            : (size_t)NIPPY_DEFAULT_MAX_SOURCE_BYTES,
        &errors);
    if (source == NULL) {
        print_errors(&errors);
        return 1;
    }

    c_path = append_suffix(output_path, ".c");
    h_path = append_suffix(output_path, ".h");
    if (c_path == NULL || h_path == NULL) {
        error_report(&errors, MESSAGE_DRIVER_COULD_NOT_ALLOCATE_PATHS);
        free(source);
        free(c_path);
        free(h_path);
        print_errors(&errors);
        return 1;
    }
    /* The compiler writes `output`, `output.c`, and `output.h`. Any of them
       being the source would destroy it, so refuse before writing anything. */
    {
        const char *outputs[3];
        size_t i;

        outputs[0] = output_path;
        outputs[1] = c_path;
        outputs[2] = h_path;
        for (i = 0; i < 3; i++) {
            if (is_same_file(source_path, outputs[i])) {
                error_report(&errors, MESSAGE_DRIVER_OUTPUT_IS_SOURCE,
                             outputs[i]);
                free(source);
                free(c_path);
                free(h_path);
                print_errors(&errors);
                return 1;
            }
        }
    }
    slash = strrchr(h_path, '/');
    header_name = slash == NULL ? h_path : slash + 1;
    config.header_name = header_name;
    config.source_name = source_path;
    compiled = compiler_compile(source, &config);
    free(source);

    if (!compiled.succeeded) {
        if (compiled.error_message != NULL) {
            fputs(compiled.error_message, stderr);
        } else {
            fprintf(stderr, "%s\n", MESSAGE_UNKNOWN_ERROR);
        }
        goto done;
    }

    if (!create_staged_output(output_path, h_path, &stage, &errors)) {
        goto report_driver_error;
    }
    if (!write_file(stage.header, compiled.h_source)) {
        error_report(&errors, MESSAGE_DRIVER_COULD_NOT_WRITE_HEADER, h_path);
        goto report_driver_error;
    }
    if (!write_file(stage.source, compiled.c_source)) {
        error_report(&errors, MESSAGE_DRIVER_COULD_NOT_WRITE_SOURCE, c_path);
        goto report_driver_error;
    }
    if (!build_binary(stage.source, stage.binary, &errors)) {
        goto report_driver_error;
    }
    if (!publish_staged_output(&stage, output_path, c_path, h_path, &errors)) {
        goto report_driver_error;
    }
    succeeded = 1;
    goto done;

report_driver_error:
    print_errors(&errors);
done:
    compiler_result_free(&compiled);
    free_staged_output(&stage);
    free(c_path);
    free(h_path);
    return succeeded ? 0 : 1;
}
