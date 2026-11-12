#include "test_support.h"
#include "alloc.h"
#include "analyser.h"
#include "ast.h"
#include "checker.h"
#include "compiler.h"
#include "error.h"
#include "generator.h"
#include "messages.h"
#include "typed_program.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const CompilerConfig test_config = {.header_name = "test.h",
                                           .source_name = "test.nip"};

int error_contains(const ErrorState *errors, const char *needle)
{
    return strstr(errors->message, needle) != NULL;
}

static void report_mismatch(const char *what, const char *artifact,
                            const char *actual, const char *expected)
{
    size_t offset = 0;

    while (actual[offset] != '\0' && expected[offset] != '\0' &&
           actual[offset] == expected[offset]) {
        offset++;
    }
    fprintf(stderr, "  %s: generated %s differs at byte %zu\n",
            what, artifact, offset);
    fprintf(stderr, "    expected: %.48s\n", expected + offset);
    fprintf(stderr, "    actual:   %.48s\n", actual + offset);
}

static size_t text_mismatch_offset(const char *actual, const GoldenText *golden)
{
    size_t offset = 0;
    size_t i;

    for (i = 0; i < golden->count; i++) {
        const char *part = golden->parts[i];
        size_t j;

        for (j = 0; part[j] != '\0'; j++) {
            if (actual[offset] != part[j]) {
                return offset;
            }
            offset++;
        }
    }
    return actual[offset] == '\0' ? (size_t)-1 : offset;
}

TestResult compile_succeeds_parts(const char *source,
                                         const GoldenText *expected_c,
                                         const char *expected_h,
                                         const char *what)
{
    CompilerResult result = compiler_compile(source, &test_config);
    TestResult status = TEST_FAIL;
    size_t offset;

    if (!result.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                result.error_message == NULL ? "(no diagnostic)"
                                             : result.error_message);
        goto finished;
    }
    offset = result.c_source == NULL
                 ? 0
                 : text_mismatch_offset(result.c_source, expected_c);
    if (result.c_source == NULL || offset != (size_t)-1) {
        fprintf(stderr,
                "  %s: generated C source differs at byte %zu\n", what,
                offset);
        goto finished;
    }
    if (result.h_source == NULL || strcmp(result.h_source, expected_h) != 0) {
        fprintf(stderr, "  %s: generated header does not match the golden\n",
                what);
        goto finished;
    }
    status = TEST_PASS;

finished:
    compiler_result_free(&result);
    return status;
}

/* Compile `source` in memory and compare both generated artifacts against the
   golden text the test owns. No files and no subprocess. */
TestResult compile_succeeds(const char *source, const char *expected_c,
                                   const char *expected_h, const char *what)
{
    CompilerResult result = compiler_compile(source, &test_config);
    TestResult status = TEST_FAIL;

    if (!result.succeeded) {
        fprintf(stderr, "  %s did not compile: %s\n", what,
                result.error_message == NULL ? "(no diagnostic)"
                                             : result.error_message);
        goto finished;
    }
    if (result.c_source == NULL || strcmp(result.c_source, expected_c) != 0) {
        report_mismatch(what, "C source",
                        result.c_source == NULL ? "" : result.c_source,
                        expected_c);
        goto finished;
    }
    if (result.h_source == NULL || strcmp(result.h_source, expected_h) != 0) {
        report_mismatch(what, "header",
                        result.h_source == NULL ? "" : result.h_source,
                        expected_h);
        goto finished;
    }
    status = TEST_PASS;

finished:
    compiler_result_free(&result);
    return status;
}

/* Compile `source`, expecting it to be rejected with a diagnostic that
   contains `format` rendered with `...`. Tests pass the MESSAGE_* macro itself
   so the assertion and the message cannot drift apart. */
TestResult compile_fails(const char *source, const char *what,
                                const char *format, ...)
{
    CompilerResult result;
    char expected[256];
    va_list args;
    TestResult status = TEST_FAIL;

    va_start(args, format);
    vsnprintf(expected, sizeof(expected), format, args);
    va_end(args);

    result = compiler_compile(source, &test_config);
    if (result.succeeded) {
        fprintf(stderr, "  %s unexpectedly compiled\n", what);
        goto finished;
    }
    if (result.error_message == NULL ||
        strstr(result.error_message, expected) == NULL) {
        fprintf(stderr, "  %s: expected diagnostic containing \"%s\"\n",
                what, expected);
        if (result.error_message != NULL) {
            fprintf(stderr, "    actual: %s\n", result.error_message);
        }
        goto finished;
    }
    status = TEST_PASS;

finished:
    compiler_result_free(&result);
    return status;
}
