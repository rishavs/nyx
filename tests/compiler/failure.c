/* Failure is scoped: a compilation that fails leaves nothing behind that can
   change the next one, and every failure is reported whole. The program below
   uses every construct that keeps compiler state while it works (nested blocks,
   defers, scopes, loops, a parallel block, type nesting), so a failure in any
   of them has state to leak. */

#include "compile_support.h"

static const char *const reference =
    "fun leaf(n: Int32): Int32\n"
    " defer do print(n) end\n"
    " return n\n"
    "end\n"
    "global s: Store = Store(64)\n"
    "fun f(n: Int32): Int32\n"
    " var t: Int32 = 0\n"
    " for i in range(0, n) do\n"
    "  scope q = Store(16) do\n"
    "   defer do print(i) end\n"
    "   if i > 1 then break end\n"
    "   t = t + leaf(i)\n"
    "  end\n"
    " end\n"
    " return t\n"
    "end\n"
    "var xs: List<Int32> = s.list<Int32>(2)\n"
    "var window: Slice<Int32> = xs.slice(0, 0)\n"
    "fun sq(n: Int32): Int32 return n * n end\n"
    "var a: Int32 = 0\n"
    "parallel do\n"
    " with a = sq(3)\n"
    "end\n"
    "print(f(3))\n";

/* The C the reference compiles to, or NULL if it does not compile. */
static char *compile_reference(void)
{
    CompilerResult result = compiler_compile(reference, NULL);
    char *copy = NULL;

    if (result.succeeded && result.c_source != NULL) {
        copy = nyx_malloc(strlen(result.c_source) + 1);
        if (copy != NULL) strcpy(copy, result.c_source);
    }
    compiler_result_free(&result);
    return copy;
}

static bool reference_unchanged(const char *baseline)
{
    char *again = compile_reference();
    bool same = again != NULL && strcmp(again, baseline) == 0;

    free(again);
    return same;
}

/* A failed result holds a message and no output; a successful one holds both
   sources. Nothing in between. */
static bool result_is_whole(const CompilerResult *result)
{
    if (result->succeeded) {
        return result->c_source != NULL && result->h_source != NULL;
    }
    return result->c_source == NULL && result->h_source == NULL &&
           result->error_message != NULL && result->error_message[0] != '\0';
}

TEST("a source that fails halfway leaves no state for the next compile")
{
    size_t length = strlen(reference);
    char *baseline = compile_reference();
    char *mutated = nyx_malloc(length + 1);
    size_t at;
    TestResult result = TEST_PASS;

    if (baseline == NULL || mutated == NULL) {
        free(baseline);
        free(mutated);
        return TEST_ERROR;
    }
    for (at = 0; at < length && result == TEST_PASS; at++) {
        CompilerResult failed;

        /* Cut the program off at every byte, then delete every byte. */
        memcpy(mutated, reference, at);
        mutated[at] = '\0';
        failed = compiler_compile(mutated, NULL);
        if (!result_is_whole(&failed) || !reference_unchanged(baseline)) {
            fprintf(stderr, "  truncated at byte %zu\n", at);
            result = TEST_FAIL;
        }
        compiler_result_free(&failed);
        if (result != TEST_PASS) break;
        memcpy(mutated, reference, at);
        memcpy(mutated + at, reference + at + 1, length - at - 1);
        mutated[length - 1] = '\0';
        failed = compiler_compile(mutated, NULL);
        if (!result_is_whole(&failed) || !reference_unchanged(baseline)) {
            fprintf(stderr, "  byte %zu deleted\n", at);
            result = TEST_FAIL;
        }
        compiler_result_free(&failed);
    }
    free(baseline);
    free(mutated);
    return result;
}

TEST("an allocation failure at any point leaves no state for the next compile")
{
    char *baseline = compile_reference();
    size_t position;
    TestResult result = TEST_PASS;

    if (baseline == NULL) return TEST_ERROR;
    for (position = 0; position < 100000 && result == TEST_PASS; position++) {
        CompilerResult failed;
        bool finished;

        nyx_alloc_fail_after(position);
        failed = compiler_compile(reference, NULL);
        nyx_alloc_reset();
        finished = failed.succeeded;
        if (!result_is_whole(&failed) || !reference_unchanged(baseline)) {
            fprintf(stderr, "  allocation %zu failed\n", position);
            result = TEST_FAIL;
        }
        compiler_result_free(&failed);
        if (finished) break;
    }
    free(baseline);
    return result;
}
