/* Guard the diagnostic catalog against common inline-message regressions. */

#include "compile_support.h"

#include <dirent.h>

static bool check_diagnostic_source(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[4096];
    bool in_report = false;
    bool has_literal = false;
    bool has_catalog_entry = false;
    size_t line_number = 0;
    size_t report_line = 0;
    bool ok = true;

    if (file == NULL) {
        fprintf(stderr, "  cannot read %s\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        line_number++;
        if (strstr(line, "nippy: ") != NULL) {
            fprintf(stderr, "  inline runtime prefix at %s:%zu\n", path, line_number);
            ok = false;
        }
        if (!in_report && strstr(line, "error_report") != NULL &&
            strchr(line, '(') != NULL) {
            in_report = true;
            has_literal = false;
            has_catalog_entry = false;
            report_line = line_number;
        }
        if (in_report) {
            has_literal = has_literal || strchr(line, '"') != NULL;
            has_catalog_entry = has_catalog_entry || strstr(line, "MESSAGE_") != NULL;
            if (strchr(line, ';') != NULL) {
                if (has_literal && !has_catalog_entry) {
                    fprintf(stderr, "  possible inline diagnostic at %s:%zu\n",
                            path, report_line);
                    ok = false;
                }
                in_report = false;
            }
        }
    }
    if (ferror(file)) ok = false;
    fclose(file);
    return ok;
}

static bool check_diagnostic_directory(const char *path, bool recurse)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    bool ok = true;

    if (dir == NULL) {
        fprintf(stderr, "  cannot read %s\n", path);
        return false;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[512];
        size_t length = strlen(entry->d_name);
        int written;

        if (entry->d_name[0] == '.') continue;
        written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child)) {
            ok = false;
        } else if (length > 2 && strcmp(entry->d_name + length - 2, ".c") == 0) {
            if (!check_diagnostic_source(child)) ok = false;
        } else if (recurse && strchr(entry->d_name, '.') == NULL) {
            if (!check_diagnostic_directory(child, false)) ok = false;
        }
    }
    closedir(dir);
    return ok;
}

TEST("compiler diagnostics use the message catalog")
{
    return check_diagnostic_directory("compiler", true) ? TEST_PASS : TEST_FAIL;
}
