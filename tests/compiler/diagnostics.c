/* Guard the diagnostic catalog against common inline-message regressions. */

#include "compile_support.h"

#include <ctype.h>
#include <dirent.h>
#include <string.h>

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
        if (strstr(line, "nyx: ") != NULL) {
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

TEST("compiler diagnostics use the message catalog")
{
    return check_directory("compiler", true, check_diagnostic_source)
               ? TEST_PASS
               : TEST_FAIL;
}

/* A phrase is two words of three or more letters with a single space between
   them. Wording like that belongs in messages.h even when it is only a fragment
   a message substitutes into itself. */
static bool has_phrase(const char *text)
{
    size_t i = 0;

    while (text[i] != '\0') {
        size_t start = i;

        while (isalpha((unsigned char)text[i])) i++;
        if (i - start >= 3 && text[i] == ' ') {
            size_t next = i + 1;
            size_t end = next;

            while (isalpha((unsigned char)text[end])) end++;
            if (end - next >= 3) return true;
        }
        if (i == start) i++;
    }
    return false;
}

static bool is_generator_path(const char *path)
{
    return strstr(path, "/generator") != NULL;
}

/* Reports a string literal on `line` that holds wording of its own. Generator
   files hold C that the program emits, so there only a printed placeholder such
   as "<function>" counts. */
static bool literal_is_wording(const char *path, const char *literal)
{
    if (is_generator_path(path)) {
        return strstr(literal, "\\\"<") != NULL &&
               (islower((unsigned char)literal[strstr(literal, "\\\"<") -
                                               literal + 3]) ||
                literal[strstr(literal, "\\\"<") - literal + 3] == '.');
    }
    return has_phrase(literal) || strstr(literal, "'%s'") != NULL ||
           (strchr(literal, '<') != NULL && islower((unsigned char)
               literal[strchr(literal, '<') - literal + 1]));
}

static bool check_stray_wording(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[4096];
    size_t line_number = 0;
    bool in_comment = false;
    bool ok = true;

    if (file == NULL) {
        fprintf(stderr, "  cannot read %s\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        const char *cursor = line;
        char literal[4096];
        bool was_comment = in_comment;

        line_number++;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (strstr(line, "/*") != NULL && strstr(line, "*/") == NULL) {
            in_comment = true;
        } else if (strstr(line, "*/") != NULL) {
            in_comment = false;
        }
        if (was_comment || strncmp(cursor, "/*", 2) == 0 ||
            strncmp(cursor, "#include", 8) == 0) {
            continue;
        }
        for (; *cursor != '\0'; cursor++) {
            size_t length = 0;

            if (*cursor != '"') continue;
            for (cursor++; *cursor != '\0' && *cursor != '"'; cursor++) {
                if (*cursor == '\\' && cursor[1] != '\0') {
                    literal[length++] = *cursor++;
                }
                literal[length++] = *cursor;
            }
            literal[length] = '\0';
            if (literal_is_wording(path, literal)) {
                fprintf(stderr, "  wording outside messages.h at %s:%zu: \"%s\"\n",
                        path, line_number, literal);
                ok = false;
            }
            if (*cursor == '\0') break;
        }
    }
    fclose(file);
    return ok;
}

TEST("compiler wording lives in the message catalog")
{
    return check_directory("compiler", true, check_stray_wording)
               ? TEST_PASS
               : TEST_FAIL;
}
