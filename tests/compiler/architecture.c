/* The compiler's architecture rules, enforced on its own source: stages reach
   only their predecessor's artifact, stage inputs are not written through,
   and no stage keeps state outside the call that uses it. These scan the text
   of compiler/, like the diagnostics tests, so they are heuristics: they catch
   the usual way of breaking a rule, not every way. */

#include "compile_support.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    const char *stage;       /* directory name under compiler/ */
    const char *forbidden[8]; /* headers this stage must not include */
} StageRule;

/* Each stage sees its predecessor's artifact and nothing earlier. The checker
   reads the syntax tree; the analyser and generator read the typed program
   (the generator also the analyser's facts, which are plain data in
   analysis.h, not the analyser's code). */
static const StageRule stage_rules[] = {
    {"lexer", {"ast.h", "parser.h", "checker.h", "analyser.h", "generator.h",
               "typed_program.h", "analysis.h", NULL}},
    {"parser", {"checker.h", "analyser.h", "generator.h", "typed_program.h",
                "analysis.h", NULL}},
    {"checker", {"lexer.h", "parser.h", "analyser.h", "generator.h",
                 "analysis.h", NULL}},
    {"analyser", {"ast.h", "lexer.h", "parser.h", "checker.h", "generator.h",
                  NULL}},
    {"generator", {"ast.h", "lexer.h", "parser.h", "checker.h", "analyser.h",
                   NULL}}
};

static const StageRule *stage_of(const char *path)
{
    size_t i;

    for (i = 0; i < sizeof(stage_rules) / sizeof(stage_rules[0]); i++) {
        char needle[64];

        snprintf(needle, sizeof(needle), "compiler/%s/", stage_rules[i].stage);
        if (strstr(path, needle) != NULL) return &stage_rules[i];
    }
    return NULL;
}

/* The header named by a `#include "x.h"` line, or NULL. */
static const char *included_header(const char *line, char *buffer, size_t size)
{
    const char *quote;
    const char *end;
    const char *base;
    size_t length;

    while (isspace((unsigned char)*line)) line++;
    if (strncmp(line, "#include", 8) != 0) return NULL;
    quote = strchr(line, '"');
    end = quote == NULL ? NULL : strchr(quote + 1, '"');
    if (end == NULL) return NULL;
    base = end;
    while (base > quote + 1 && base[-1] != '/') base--;
    length = (size_t)(end - base);
    if (length + 1 > size) return NULL;
    memcpy(buffer, base, length);
    buffer[length] = '\0';
    return buffer;
}

static bool check_includes(const char *path)
{
    const StageRule *rule = stage_of(path);
    FILE *file = fopen(path, "r");
    char line[1024];
    size_t number = 0;
    bool ok = true;

    if (rule == NULL) return true;
    if (file == NULL) {
        fprintf(stderr, "  cannot read %s\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char header[128];
        const char *name = included_header(line, header, sizeof(header));
        size_t i;

        number++;
        for (i = 0; name != NULL && rule->forbidden[i] != NULL; i++) {
            if (strcmp(name, rule->forbidden[i]) == 0) {
                fprintf(stderr, "  %s:%zu: the %s stage includes %s\n", path,
                        number, rule->stage, name);
                ok = false;
            }
        }
    }
    fclose(file);
    return ok;
}

/* Where a stage-private header may also be scanned, since the includes that
   matter usually sit there. */
static const char *const internal_headers[] = {
    "compiler/lexer/lexer_internal.h", "compiler/parser/parser_internal.h",
    "compiler/checker/checker_internal.h",
    "compiler/analyser/analyser_internal.h",
    "compiler/generator/generator_internal.h"
};

TEST("a stage includes only its own artifact")
{
    bool ok = check_directory("compiler", true, check_includes);
    size_t i;

    for (i = 0; i < sizeof(internal_headers) / sizeof(internal_headers[0]);
         i++) {
        FILE *probe = fopen(internal_headers[i], "r");

        if (probe == NULL) continue; /* a stage without a private header */
        fclose(probe);
        if (!check_includes(internal_headers[i])) ok = false;
    }
    return ok ? TEST_PASS : TEST_FAIL;
}

/* Text that writes through a stage input: a cast that drops the const of the
   artifact the stage was handed. The checker and parser fill their own output
   and may cast it; only the stages that merely read are checked. */
static const char *const const_casts[] = {
    "(TypedProgram *)", "(TypedNode *)", "(TypedType *)",
    "(TypedFunctionSignature *)", "(Ast *)", "(AstNode *)", NULL
};

static bool check_read_only_stage(const char *path)
{
    FILE *file;
    char line[4096];
    size_t number = 0;
    bool ok = true;

    if (strstr(path, "compiler/analyser/") == NULL &&
        strstr(path, "compiler/generator/") == NULL) {
        return true;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "  cannot read %s\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t i;

        number++;
        for (i = 0; const_casts[i] != NULL; i++) {
            if (strstr(line, const_casts[i]) != NULL) {
                fprintf(stderr, "  %s:%zu: casts away const with %s\n", path,
                        number, const_casts[i]);
                ok = false;
            }
        }
    }
    fclose(file);
    return ok;
}

TEST("the analyser and generator never write to the typed program")
{
    return check_directory("compiler", true, check_read_only_stage)
               ? TEST_PASS : TEST_FAIL;
}

/* A file-scope variable that is not const: `static` (or `_Thread_local`) at
   the start of a line with no parameter list and no `const`. The allocation
   failure hook and the driver's rename hook are test seams and may keep
   state; nothing else may. */
static bool is_mutable_static(const char *line)
{
    if (strncmp(line, "static ", 7) != 0 &&
        strncmp(line, "_Thread_local ", 14) != 0) {
        return false;
    }
    return strchr(line, '(') == NULL && strstr(line, "const ") == NULL &&
           strstr(line, "typedef") == NULL;
}

static bool check_no_hidden_state(const char *path)
{
    FILE *file;
    char line[4096];
    size_t number = 0;
    bool ok = true;

    if (strcmp(path, "compiler/alloc.c") == 0 ||
        strcmp(path, "compiler/main.c") == 0) {
        return true;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "  cannot read %s\n", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        number++;
        if (is_mutable_static(line)) {
            fprintf(stderr, "  %s:%zu: file-scope mutable state: %s", path,
                    number, line);
            ok = false;
        }
    }
    fclose(file);
    return ok;
}

TEST("no stage keeps mutable state outside the call that uses it")
{
    return check_directory("compiler", true, check_no_hidden_state)
               ? TEST_PASS : TEST_FAIL;
}

/* The public entry points name their input `const`, which is what makes the
   read-only rule checkable by the compiler rather than by review. */
static bool header_declares(const char *path, const char *declaration)
{
    FILE *file = fopen(path, "r");
    char text[16384];
    size_t length;
    bool found;

    if (file == NULL) return false;
    length = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[length] = '\0';
    found = strstr(text, declaration) != NULL;
    if (!found) fprintf(stderr, "  %s lacks: %s\n", path, declaration);
    return found;
}

TEST("every stage takes its input through a const pointer")
{
    bool ok = header_declares("include/checker.h",
                              "bool checker_check(const Ast *ast");
    ok = header_declares("include/analyser.h",
                         "bool analyser_analyse(const TypedProgram *program") &&
         ok;
    ok = header_declares("include/analyser.h",
                         "bool analyser_validate(const TypedProgram *program") &&
         ok;
    ok = header_declares("include/generator.h",
                         "bool generator_generate(const TypedProgram *program") &&
         ok;
    ok = header_declares("include/generator.h", "const Analysis *analysis") &&
         ok;
    return ok ? TEST_PASS : TEST_FAIL;
}
