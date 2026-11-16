#define _POSIX_C_SOURCE 200809L
/* CRAP score report. CRAP(f) = complexity^2 * (1 - coverage)^3 + complexity,
   where complexity is cyclomatic complexity and coverage is the fraction of the
   function's lines that the test suites executed. See BUILD.md ("CRAP scores").

   usage: crap THRESHOLD GCOV_REPORT SOURCE.c...

   Parses each SOURCE with libclang to count decision points, reads per-function
   line coverage from `gcov -f` output, prints functions at or above THRESHOLD
   (worst first) and exits 1 if there are any. */

#include <clang-c/Index.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char file[512];
    char name[128];
    unsigned line;
    unsigned complexity;
    double coverage; /* 0..1, or -1 when gcov has no record of the function */
    double crap;
} Entry;

typedef struct {
    char file[512];
    char name[128];
    double coverage;
} Coverage;

static Entry *entries;
static size_t entry_count, entry_capacity;
static Coverage *records;
static size_t record_count, record_capacity;

static void *grow(void *array, size_t *capacity, size_t size)
{
    if (*capacity == 0) {
        *capacity = 256;
    } else {
        *capacity *= 2;
    }
    array = realloc(array, *capacity * size);
    if (array == NULL) {
        fprintf(stderr, "crap: out of memory\n");
        exit(2);
    }
    return array;
}

typedef struct {
    unsigned complexity;
} Walk;

static enum CXChildVisitResult count_decisions(CXCursor cursor, CXCursor parent,
                                               CXClientData data)
{
    Walk *walk = data;

    (void)parent;
    switch (clang_getCursorKind(cursor)) {
    case CXCursor_IfStmt:
    case CXCursor_ForStmt:
    case CXCursor_WhileStmt:
    case CXCursor_DoStmt:
    case CXCursor_CaseStmt:
    case CXCursor_ConditionalOperator:
        walk->complexity++;
        break;
    case CXCursor_BinaryOperator: {
        enum CXBinaryOperatorKind kind = clang_getCursorBinaryOperatorKind(cursor);

        if (kind == CXBinaryOperator_LAnd || kind == CXBinaryOperator_LOr) {
            walk->complexity++;
        }
        break;
    }
    default:
        break;
    }
    return CXChildVisit_Recurse;
}

static void relative_path(CXString raw, char *out, size_t capacity)
{
    const char *path = clang_getCString(raw);
    const char *marker = strstr(path, "/compiler/");

    if (marker == NULL) {
        marker = strstr(path, "/tools/");
    }
    if (marker == NULL) {
        marker = strstr(path, "/tests/");
    }
    snprintf(out, capacity, "%s", marker != NULL ? marker + 1 : path);
}

static enum CXChildVisitResult visit(CXCursor cursor, CXCursor parent,
                                     CXClientData data)
{
    (void)parent;
    (void)data;
    if (clang_getCursorKind(cursor) == CXCursor_FunctionDecl &&
        clang_isCursorDefinition(cursor) &&
        clang_Location_isFromMainFile(clang_getCursorLocation(cursor))) {
        Walk walk = {1};
        Entry *entry;
        CXString name = clang_getCursorSpelling(cursor);
        CXSourceLocation location = clang_getCursorLocation(cursor);
        CXFile file;
        unsigned line;
        CXString file_name;

        clang_visitChildren(cursor, count_decisions, &walk);
        if (entry_count == entry_capacity) {
            entries = grow(entries, &entry_capacity, sizeof(*entries));
        }
        entry = &entries[entry_count++];
        clang_getSpellingLocation(location, &file, &line, NULL, NULL);
        file_name = clang_getFileName(file);
        relative_path(file_name, entry->file, sizeof(entry->file));
        snprintf(entry->name, sizeof(entry->name), "%s", clang_getCString(name));
        entry->line = line;
        entry->complexity = walk.complexity;
        entry->coverage = -1;
        clang_disposeString(file_name);
        clang_disposeString(name);
        return CXChildVisit_Continue;
    }
    return CXChildVisit_Continue;
}

/* gcov -f prints "Function 'name'" and "Lines executed:P% of N" for each
   function, then a "File 'path'" line closing the group for that file. */
static void read_gcov(const char *path)
{
    FILE *report = fopen(path, "r");
    char text[1024];
    char pending[256][128];
    double pending_coverage[256];
    size_t pending_count = 0;

    if (report == NULL) {
        fprintf(stderr, "crap: cannot read %s\n", path);
        exit(2);
    }
    while (fgets(text, sizeof(text), report) != NULL) {
        char name[128];
        char file[512];
        double percent;

        if (sscanf(text, "Function '%127[^']'", name) == 1) {
            if (pending_count < 256) {
                snprintf(pending[pending_count], sizeof(pending[0]), "%s", name);
                pending_coverage[pending_count] = -1;
            }
        } else if (sscanf(text, "Lines executed:%lf%%", &percent) == 1) {
            if (pending_count < 256) {
                pending_coverage[pending_count++] = percent / 100.0;
            }
        } else if (sscanf(text, "File '%511[^']'", file) == 1) {
            size_t i;
            const char *marker = strstr(file, "/compiler/");

            if (marker == NULL) {
                marker = strstr(file, "/tools/");
            }
            for (i = 0; i < pending_count; i++) {
                Coverage *record;

                if (record_count == record_capacity) {
                    records = grow(records, &record_capacity, sizeof(*records));
                }
                record = &records[record_count++];
                snprintf(record->file, sizeof(record->file), "%s",
                         marker != NULL ? marker + 1 : file);
                snprintf(record->name, sizeof(record->name), "%s", pending[i]);
                record->coverage = pending_coverage[i];
            }
            pending_count = 0;
        }
    }
    fclose(report);
}

static int by_crap(const void *left, const void *right)
{
    const Entry *a = left;
    const Entry *b = right;

    return a->crap < b->crap ? 1 : a->crap > b->crap ? -1 : 0;
}

int main(int argc, char **argv)
{
    double threshold;
    CXIndex index;
    size_t i, j;
    size_t failing = 0;
    char resource[600] = "-resource-dir=";
    const char *flags[] = {"-std=c11", "-Iinclude", "-Itests/harness",
                           "-Itests/support", resource};
    FILE *query = popen("clang -print-resource-dir", "r");

    if (argc < 4) {
        fprintf(stderr, "usage: crap THRESHOLD GCOV_REPORT SOURCE.c...\n");
        return 2;
    }
    if (query == NULL || fgets(resource + strlen(resource),
                               (int)(sizeof(resource) - strlen(resource)),
                               query) == NULL) {
        fprintf(stderr, "crap: cannot find clang's resource directory\n");
        return 2;
    }
    pclose(query);
    resource[strcspn(resource, "\n")] = '\0';
    threshold = atof(argv[1]);
    read_gcov(argv[2]);
    index = clang_createIndex(0, 0);
    for (i = 3; i < (size_t)argc; i++) {
        CXTranslationUnit unit = clang_parseTranslationUnit(
            index, argv[i], flags, 5, NULL, 0, CXTranslationUnit_None);

        if (unit == NULL) {
            fprintf(stderr, "crap: cannot parse %s\n", argv[i]);
            return 2;
        }
        if (clang_getNumDiagnostics(unit) != 0) {
            CXDiagnostic first = clang_getDiagnostic(unit, 0);
            CXString text = clang_formatDiagnostic(first, 0);

            if (clang_getDiagnosticSeverity(first) >= CXDiagnostic_Error) {
                fprintf(stderr, "crap: %s: %s\n", argv[i], clang_getCString(text));
                return 2;
            }
            clang_disposeString(text);
            clang_disposeDiagnostic(first);
        }
        clang_visitChildren(clang_getTranslationUnitCursor(unit), visit, NULL);
        clang_disposeTranslationUnit(unit);
    }
    for (i = 0; i < entry_count; i++) {
        Entry *entry = &entries[i];
        double complexity = entry->complexity;
        double uncovered;

        for (j = 0; j < record_count; j++) {
            if (strcmp(records[j].name, entry->name) == 0 &&
                strcmp(records[j].file, entry->file) == 0) {
                entry->coverage = records[j].coverage;
                break;
            }
        }
        uncovered = entry->coverage < 0 ? 1.0 : 1.0 - entry->coverage;
        entry->crap = complexity * complexity * uncovered * uncovered * uncovered +
                      complexity;
        if (entry->crap >= threshold) {
            failing++;
        }
    }
    qsort(entries, entry_count, sizeof(*entries), by_crap);
    printf("%zu functions, %zu at or above CRAP %.0f\n", entry_count, failing,
           threshold);
    for (i = 0; i < entry_count && entries[i].crap >= threshold; i++) {
        printf("%7.1f  complexity %3u  coverage %5.1f%%  %s:%u  %s\n",
               entries[i].crap, entries[i].complexity,
               entries[i].coverage < 0 ? 0.0 : entries[i].coverage * 100.0,
               entries[i].file, entries[i].line, entries[i].name);
    }
    return failing == 0 ? 0 : 1;
}
